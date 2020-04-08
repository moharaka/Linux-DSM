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

static int __ktcp_send(struct socket *sock, const char *buffer, size_t length,
		unsigned long flags)
{
	struct kvec vec;
	int len, written = 0, left = length;
	int ret;

	struct msghdr msg = {
		.msg_name    = 0,
		.msg_namelen = 0,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags   = flags,
	};

repeat_send:
	vec.iov_len = left;
	vec.iov_base = (char *)buffer + written;

	len = kernel_sendmsg(sock, &msg, &vec, 1, left);
	if (len == -EAGAIN || len == -ERESTARTSYS) {
		goto repeat_send;
	}
	if (len > 0) {
		written += len;
		left -= len;
		if (left != 0) {
			goto repeat_send;
		}
	}

	ret = written != 0 ? written : len;
	if (ret > 0 && ret != length) {
		printk(KERN_WARNING "ktcp_send send %d bytes which expected_size=%lu bytes", ret, length);
	}

	if (ret < 0) {
		printk(KERN_ERR "ktcp_send %d", ret);
	}

	return ret;
}

int ktcp_send(struct socket *sock, const char *buffer, size_t length,
		unsigned long flags, const tx_add_t * tx_add)
{
	struct ktcp_hdr hdr = {
		.length = length,
		.extent = *tx_add,
	};
	int ret;
	mm_segment_t oldmm;
	char *local_buffer;

	printk(KERN_DEBUG "%s:%d: txid %d started\n", __func__, current->pid, tx_add->txid);

	local_buffer = kmalloc(KTCP_BUFFER_SIZE, GFP_KERNEL);
	if (!local_buffer) {
		return -ENOMEM;
	}

	BUG_ON((length  + sizeof(hdr)) > KTCP_BUFFER_SIZE);

	// Get current address access limit
	oldmm = get_fs();
	set_fs(KERNEL_DS);

	memcpy(local_buffer, &hdr, sizeof(hdr));
	memcpy(local_buffer + sizeof(hdr), buffer, length);

	ret = __ktcp_send(sock, local_buffer, KTCP_BUFFER_SIZE, flags);
	if (ret < 0)
		goto out;

out:
	// Retrieve address access limit
	set_fs(oldmm);
	kfree(local_buffer);
	return ret < 0 ? ret : hdr.length;
}

static int __ktcp_receive__(struct socket *sock, char *buffer, size_t expected_size,
		unsigned long flags)
{
	struct kvec vec;
	int ret;
	int len = 0;

	struct msghdr msg = {
		.msg_name    = 0,
		.msg_namelen = 0,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags   = flags,
	};

	if (expected_size == 0) {
		return 0;
	}

read_again:
	vec.iov_len = expected_size - len;
	vec.iov_base = buffer + len;
	ret = kernel_recvmsg(sock, &msg, &vec, 1, expected_size - len, flags);

	if (ret == 0) {
		return len;
	}

	// Non-blocking on the first try
	if (len == 0 && (flags & SOCK_NONBLOCK) &&
			(ret == -EWOULDBLOCK || ret == -EAGAIN)) {
		return ret;
	}

	if (ret == -EAGAIN || ret == -ERESTARTSYS) {
		goto read_again;
	}
	else if (ret < 0) {
		printk(KERN_ERR "kernel_recvmsg %d", ret);
		return ret;
	}
	len += ret;
	if (len != expected_size) {
		printk(KERN_WARNING "ktcp_receive receive %d bytes which expected_size=%lu bytes, read again", len, expected_size);
		goto read_again;
	}

	return len;
}

//static DEFINE_SEMAPHORE(receive_lock);

//Allocate a buffer and store the any received message
static struct ktcp_hdr*  __ktcp_receive_get(struct socket *sock, unsigned long flags)
{
	int ret;
	char *local_buffer = kmalloc(KTCP_BUFFER_SIZE, GFP_KERNEL);
	if (!local_buffer) {
		return NULL;
	}

	printk(KERN_DEBUG "%s:%d:  lock held\n", __func__, current->pid);
	ret = __ktcp_receive__(sock, local_buffer, KTCP_BUFFER_SIZE, flags);
	printk(KERN_DEBUG "%s:%d: lock releasedheld\n", __func__, current->pid);
	if (ret < 0) {
		return NULL;
	}

	return (struct ktcp_hdr*) local_buffer;
}

#ifdef USE_CACHE
#define BLOCKED_HASH_BITS	7
static DEFINE_HASHTABLE(ktcp_hash, BLOCKED_HASH_BITS);
static DEFINE_SPINLOCK(ktcp_hash_lock);

struct ktcp_cache_entry_s
{
	struct ktcp_hdr* hdr;
	struct hlist_node hlink;
};

static void ktcp_cache_put(uint16_t txid, struct ktcp_hdr* hdr)
{
 	struct ktcp_cache_entry_s *entry;
	entry = kmalloc(sizeof(struct ktcp_cache_entry_s), GFP_KERNEL);
	entry->hdr=hdr;

	spin_lock(&ktcp_hash_lock);
	hash_add(ktcp_hash, &entry->hlink, hdr->extent.txid);
	spin_unlock(&ktcp_hash_lock);
}

static struct ktcp_hdr* ktcp_cache_pop(uint16_t txid)
{
	int found=0;
	struct ktcp_hdr *hdr=NULL;
	struct ktcp_cache_entry_s *entry=NULL;

	spin_lock(&ktcp_hash_lock);
	hash_for_each_possible(ktcp_hash, entry, hlink, txid) {
		hdr=entry->hdr;
		if(txid==hdr->extent.txid || txid==0xFF)
		{
			found=1;
			break;
		}
	}
	if(found)
		hash_del(&entry->hlink);
	else
		hdr=NULL;
	spin_unlock(&ktcp_hash_lock);

	kfree(entry); entry=NULL;
	return hdr;
}
#endif


//and return the corresponding local_buffer
int ktcp_receive(kconnection_t *conn, char* buffer, unsigned long flags, 
			tx_add_t *tx_add)
{
	int ret=0;
	struct ktcp_hdr *hdr;
	uint16_t txid=tx_add->txid;
	uint16_t length=0;
	struct socket *sock=&conn->sock;
	struct sema *sema=&conn->sema;

	printk(KERN_DEBUG "%s:%d: txid %d started\n", __func__, current->pid, tx_add->txid);
	//Execute receive_get and cache_get until the right transaction is found
	do{
		//Get from network
		down(&receive_lock);
		hdr=(struct ktcp_hdr*) __ktcp_receive_get(sock, flags);
		up(&receive_lock);
#ifdef USE_CACHE
		if(hdr->extent.txid==txid || txid==0xFF)
		{
			//if found, we exit the loop
			break;
		}

		//add local buffer to the cache
		ktcp_cache_put(txid, hdr); hdr=NULL;

		//check if not already in the cache (putted by another thread)
		hdr=ktcp_cache_pop(txid);

	}while(hdr==NULL);//What if we never receive the transaction?
	BUG_ON(!hdr || (hdr->extent.txid!=txid && txid!=0xFF));
#else
	}while(0);
#endif

	printk(KERN_DEBUG "%s: txid requested %d found %d\n", __func__, txid, hdr->extent.txid);
	
	length = hdr->length;
	/* hdr.length is undetermined on process killed */
	if (unlikely(length > PAGE_SIZE)) {
		printk(KERN_WARNING "%s: buffer to small\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	memcpy(buffer, (char*)hdr + sizeof(hdr), length);

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

int ktcp_accept(kconnection_t *conn, struct socket **accept_socket, unsigned long flag)
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
