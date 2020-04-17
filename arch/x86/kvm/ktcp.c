/*
 * TCP support for KVM software distributed memory
 *
 * This feature allows us to run multiple KVM instances on different machines
 * sharing the same address space.
 *
 * Authors:
 *   Chen Yubin <i@binss.me>
 *   Ding Zhuocheng <tcbbdddd@gmail.com>
 *   Zhang Jin <437629012@qq.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <net/sock.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <asm/uaccess.h>
#include <linux/socket.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/kvm_host.h>

#include "ktcp.h"

#define USE_CACHE

struct ktcp_hdr {
	tx_add_t extent;
	uint16_t length;
} __attribute__((packed));

#define KTCP_BUFFER_SIZE (sizeof(struct ktcp_hdr) + PAGE_SIZE)

#define BLOCKED_HASH_BITS	7
static DEFINE_HASHTABLE(ktcp_hash, BLOCKED_HASH_BITS);
static DEFINE_SPINLOCK(ktcp_hash_lock);

struct ktcp_cache_entry_s
{
	struct ktcp_hdr* hdr;
	struct hlist_node hlink;
};

DEFINE_SEMAPHORE(cache_sema);

static void ktcp_cache_put(uint16_t txid, struct ktcp_hdr* hdr)
{
 	struct ktcp_cache_entry_s *entry;
	entry = kmalloc(sizeof(struct ktcp_cache_entry_s), GFP_KERNEL);
	entry->hdr=hdr;

	spin_lock(&ktcp_hash_lock);
	hash_add(ktcp_hash, &entry->hlink, hdr->extent.txid);
	spin_unlock(&ktcp_hash_lock);
	up(&cache_sema);
}

static struct ktcp_hdr* ktcp_cache_pop(uint16_t txid)
{
	int found=0;
	int bkt;
	struct ktcp_hdr *hdr=NULL;
	struct ktcp_cache_entry_s *entry=NULL;

	down(&cache_sema);
	spin_lock(&ktcp_hash_lock);
	hash_for_each(ktcp_hash, bkt, entry, hlink) {
		hdr=entry->hdr;
		if(txid==hdr->extent.txid || txid==0xFF)
		{
			found=1;
			printk(KERN_DEBUG "%s:%d:  found in cache\n", __func__,hdr->extent.txid);
			break;
		}
	}
	if(found)
		hash_del(&entry->hlink);
	else
		hdr=NULL;
	spin_unlock(&ktcp_hash_lock);

	if(!found)
		up(&cache_sema);

	kfree(entry); entry=NULL;
	return hdr;
}

static int __ktcp_send(struct socket *sock, const char *buffer,
				size_t length, unsigned long flags)
{
	struct ktcp_hdr* hdr = (struct ktcp_hdr*) buffer;
	ktcp_cache_put(hdr->extent.txid, hdr); 
	return 0;
}

#define RESP_FLAG 0x8000
int ktcp_send(struct socket *sock, const char *buffer, size_t length,
		unsigned long flags, const tx_add_t * tx_add)
{
	struct ktcp_hdr hdr = {
		.length = length,
		.extent = *tx_add,
	};
	int ret;
	//mm_segment_t oldmm;
	char *local_buffer=NULL;

	printk(KERN_DEBUG "%s:%d: txid %d wf %d length %ld\n", __func__, current->pid, tx_add->txid, tx_add->txid^RESP_FLAG, length);

	hdr.extent.txid^=RESP_FLAG;

	local_buffer = kmalloc(KTCP_BUFFER_SIZE, GFP_KERNEL);
	if (!local_buffer) {
		return -ENOMEM;
	}

	BUG_ON((length  + sizeof(hdr)) > KTCP_BUFFER_SIZE);

	// Get current address access limit
	//oldmm = get_fs();
	//set_fs(KERNEL_DS);

	memcpy(local_buffer, &hdr, sizeof(hdr));
	memcpy(local_buffer + sizeof(hdr), buffer, length);

	ret = __ktcp_send(sock, local_buffer, KTCP_BUFFER_SIZE, flags);
	if (ret < 0)
		goto out;

out:
	// Retrieve address access limit
	//set_fs(oldmm);
	//kfree(local_buffer);
	return ret < 0 ? ret : hdr.length;
}



//and return the corresponding local_buffer
int ktcp_receive(struct socket *sock, char* buffer, unsigned long flags, 
			tx_add_t *tx_add)
{
	int ret=0;
	struct ktcp_hdr *hdr=NULL;
	uint16_t txid=tx_add->txid;
	uint16_t length=0;

	printk(KERN_DEBUG "%s:%d: txid %d started\n", __func__, current->pid, tx_add->txid);
	//Execute receive_get and cache_get until the right transaction is found
	do{
		hdr=ktcp_cache_pop(txid);
		//udelay(100);
	}while(hdr==NULL);

	BUG_ON(!hdr || (hdr->extent.txid!=txid && txid!=0xFF));

	length = hdr->length;

	printk(KERN_DEBUG "%s: txid requested %d found %d length %d\n", 
				__func__, txid, hdr->extent.txid, length);
	
	/* hdr.length is undetermined on process killed */
	if (unlikely(length > PAGE_SIZE)) {
		printk(KERN_WARNING "%s: buffer to small\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	memcpy(buffer, (char*)hdr + sizeof(*hdr), length);

	if (tx_add) {
		*tx_add = hdr->extent;
	}

out:
	kfree(hdr); hdr=NULL;
	return ret < 0 ? ret : length;
}

int ktcp_connect(const char *host, const char *port, struct socket **conn_socket)
{
	int ret;
	struct sockaddr_in saddr;
	long portdec;

	if (host == NULL || port == NULL || conn_socket == NULL) {
		return -EINVAL;
	}

	ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, conn_socket);
	if (ret < 0) {
		printk(KERN_DEBUG "sock_create %d\n", ret);
		return ret;
	}

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	kstrtol(port, 10, &portdec);
	saddr.sin_port = htons(portdec);
	saddr.sin_addr.s_addr = in_aton(host);

re_connect:
	ret = (*conn_socket)->ops->connect(*conn_socket, (struct sockaddr *)&saddr,
			sizeof(saddr), O_RDWR);
	if (ret == -EAGAIN || ret == -ERESTARTSYS) {
		goto re_connect;
	}

	if (ret && (ret != -EINPROGRESS)) {
		printk(KERN_DEBUG "connect %d\n", ret);
		sock_release(*conn_socket);
		return ret;
	}
	return SUCCESS;
}

int ktcp_listen(const char *host, const char *port, struct socket **listen_socket)
{
	int ret;
	struct sockaddr_in saddr;
	long portdec;

	BUILD_BUG_ON((sizeof(struct ktcp_hdr)) != (sizeof(uint16_t) + sizeof(extent_t)));

	ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, listen_socket);
	if (ret != 0) {
		printk(KERN_ERR "sock_create %d", ret);
		return ret;
	}
	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	kstrtol(port, 10, &portdec);
	saddr.sin_port = htons(portdec);
	saddr.sin_addr.s_addr = in_aton(host);

	ret = (*listen_socket)->ops->bind(*listen_socket, (struct sockaddr *)&saddr, sizeof(saddr));
	if (ret != 0) {
		printk(KERN_ERR "bind %d\n", ret);
		sock_release(*listen_socket);
		return ret;
	}

	ret = (*listen_socket)->ops->listen(*listen_socket, DEFAULT_BACKLOG);
	if (ret != 0) {
		printk(KERN_ERR "listen %d\n", ret);
		sock_release(*listen_socket);
		return ret;
	}

	return SUCCESS;
}

int ktcp_accept(struct socket *listen_socket, struct socket **accept_socket, unsigned long flag)
{
	int ret;

	if (listen_socket == NULL) {
		printk(KERN_ERR "null listen_socket\n");
		return -EINVAL;
	}

	ret = sock_create_lite(listen_socket->sk->sk_family, listen_socket->sk->sk_type,
			listen_socket->sk->sk_protocol, accept_socket);
	if (ret != 0) {
		printk(KERN_ERR "sock_create %d\n", ret);
		return ret;
	}

re_accept:
	ret = listen_socket->ops->accept(listen_socket, *accept_socket, flag);
	if (ret == -ERESTARTSYS) {
		if (kthread_should_stop())
			return ret;
		goto re_accept;
	}
	// When setting SOCK_NONBLOCK flag, accept return this when there's nothing in waiting queue.
	if (ret == -EWOULDBLOCK || ret == -EAGAIN) {
		sock_release(*accept_socket);
		*accept_socket = NULL;
		return ret;
	}
	if (ret < 0) {
		printk(KERN_ERR "accept %d\n", ret);
		sock_release(*accept_socket);
		*accept_socket = NULL;
		return ret;
	}

	(*accept_socket)->ops = listen_socket->ops;
	return SUCCESS;
}

int ktcp_release(struct socket *conn_socket)
{
	if (conn_socket == NULL) {
		return -EINVAL;
	}

	sock_release(conn_socket);
	return SUCCESS;
}
