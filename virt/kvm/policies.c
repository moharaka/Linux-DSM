/*
 *  Kernel-based Virtual Machine driver for Linux
 *
 * 
 *
 * manage policy on pages
 *
 */

#include "../../arch/x86/kvm/dsm-util.h"
#include <linux/kvm.h>


//manage policy
void kvm_apply_policy(struct kvm *kvm, page_pol_t app);
void kvm_apply_policy(struct kvm *kvm, page_pol_t app){	
	
	struct kvm_dsm_memslots *slots;
	struct kvm_dsm_memory_slot *slot;
	struct kvm_dsm_info *info;

	int i, j, k, N, n;

	slots = __kvm_hvaslots(kvm);
	
	struct kvm_page_pol act;
	
	i = 0; n = app.taille;
	
	if (slots->used_slots != 0){
		
		while(i < n){
			act =  app.tab[i];
			//operations start here on act
			j = 0;
			while (j < slots->used_slots) {
				slot = &slots->memslots[j];
				N = slot->npages;
				long list_gpn;
				k = 0;
				while (k < N) {
					info = &slot->vfn_dsm_state[k];
					list_gpn = __kvm_dsm_vfn_to_gfn(slot, false,slot->base_vfn + k, 0, NULL);
					if (list_gpn == act.gpn){
							info->policy = act.pol;
			
							printk(KERN_INFO "\tgfn\tpol\n");
							printk(KERN_INFO "\t[%llu]; \t %c;",list_gpn, info->policy);
							printk(KERN_INFO "***************");
							break;
					}
					k = k + 1;
				}
				if (k < N) break;
				j = j + 1 ;
			}
			i = i + 1;
		}
	}	
}
