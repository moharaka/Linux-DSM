/*
 *  Kernel-based Virtual Machine driver for Linux
 *
 * 
 *
 *Pour la manipulation des politiques sur les pages
 *
 */

#include "../../arch/x86/kvm/dsm-util.h"
#include <linux/kvm.h>
#include <linux/slab.h>


//manage policy
void kvm_apply_policy(struct kvm *kvm, page_pol_t *app);
void kvm_apply_policy(struct kvm *kvm, page_pol_t *app){	
	
	struct kvm_dsm_memslots *slots;
	struct kvm_dsm_memory_slot *slot;
	struct kvm_dsm_info *info;

	int i, j, k, N;
	printk(KERN_INFO "ON ENTRE DANS LA FONCTION");

	slots = __kvm_hvaslots(kvm);
	printk(KERN_INFO "A- slots->used_slots = [%d]\n", slots->used_slots);
	
	struct kvm_page_pol act = app->data;
	printk(KERN_INFO "AU Départ gpn = [%lu] et politique = {%c}", act.gpn, act.pol);
	
	j = 0;k = 0;i = 0;
	
	if (slots->used_slots != 0){
		page_pol_t *tmp;
		tmp = kzalloc(sizeof(page_pol_t), GFP_KERNEL);
		
		tmp = app;
		//while(tmp !=NULL){
			act = tmp->data;

			//operations start here on act
			bool flag1 = false;
			printk(KERN_INFO "structure i = (%d) de gpn [%lu] et de politique {%c}",i,act.gpn,act.pol);
			while (!flag1 && j < slots->used_slots) {
				slot = &slots->memslots[j];
				N = slot->npages;
				long list_gpn[N];
				bool flag2 = false;
				while (!flag2 && k < N) {
					info = &slot->vfn_dsm_state[k];
		
					list_gpn[k] = __kvm_dsm_vfn_to_gfn(slot, false,slot->base_vfn + k, 0, NULL);
					if (list_gpn[k] == act.gpn){
							printk(KERN_INFO "pour le slot j = (%d) et le npages - %d - , match (%lu)",j,k,act.gpn);
							info->policy = act.pol;
				
							printk(KERN_INFO "\tgfn\tpol\n");
							printk(KERN_INFO "\t[%llu]; \t %c;",list_gpn[k], info->policy);
							printk(KERN_INFO "***************");
							flag2 = true;
							}else k = k + 1;
					}
				if (k < N) {flag1 = true;}
				else {j = j + 1 ;}
				}
		//}
		kfree(tmp);
		/*kfree(slots);
		kfree(slot);
		kfree(info);*/
	}
	
}

//manage policy
/*void kvm_apply_policy(struct kvm *kvm, page_pol_t *app);
void kvm_apply_policy(struct kvm *kvm, page_pol_t *app){	
	
	struct kvm_dsm_memslots *slots;
	struct kvm_dsm_memory_slot *slot;
	struct kvm_dsm_info *info;

	int i, j, k, N;
	printk(KERN_INFO "ON ENTRE DANS LA FONCTION");

	slots = __kvm_hvaslots(kvm);
	printk(KERN_INFO "A- slots->used_slots = [%d]\n", slots->used_slots);
	
	struct kvm_page_pol act = app->data;
	printk(KERN_INFO "AU Départ gpn = [%lu] et politique = {%c}", act.gpn, act.pol);
	
	j = 0;k = 0;i = 0;
	
	if (slots->used_slots != 0){
		page_pol_t *tmp;
		tmp = kzalloc(sizeof(page_pol_t), GFP_KERNEL);
		
		tmp = app;
		while(tmp !=NULL){
			
			app = tmp->next;
			//tmp = app;
			++i;
			act = tmp->data;

			//operations start here on act
			bool flag1 = false;
			printk(KERN_INFO "structure i = (%d) de gpn [%lu] et de politique {%c}",i,act.gpn,act.pol);
			while (!flag1 && j < slots->used_slots) {
				slot = &slots->memslots[j];
				N = slot->npages;
				long list_gpn[N];
				bool flag2 = false;
				while (!flag2 && k < N) {
					info = &slot->vfn_dsm_state[k];
		
					list_gpn[k] = __kvm_dsm_vfn_to_gfn(slot, false,slot->base_vfn + k, 0, NULL);
					if (list_gpn[k] == act.gpn){
							printk(KERN_INFO "pour le slot j = (%d) et le npages - %d - , match (%lu)",j,k,act.gpn);
							info->policy = act.pol;
				
							printk(KERN_INFO "\tgfn\tpol\n");
							printk(KERN_INFO "\t[%llu]; \t %c;",list_gpn[k], info->policy);
							printk(KERN_INFO "***************");
							flag2 = true;
							}else k = k + 1;
					}
				if (k < N) {flag1 = true;}
				else {j = j + 1 ;}
				}
			//kfree(tmp);
			//app = delete(app);
			tmp = app;
		}
		kfree(tmp);
		/*kfree(slots);
		kfree(slot);
		kfree(info);*/
	/*}
	
}*/
