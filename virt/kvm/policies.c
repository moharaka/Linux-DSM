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
//#include <linux/slab.h>


//manage policy
void kvm_apply_policy(struct kvm *kvm, page_pol_t app);
void kvm_apply_policy(struct kvm *kvm, page_pol_t app){	
	
	struct kvm_dsm_memslots *slots;
	struct kvm_dsm_memory_slot *slot;
	struct kvm_dsm_info *info;

	int i, j, k, N, n;
	printk(KERN_INFO "ON ENTRE DANS LA FONCTION");

	slots = __kvm_hvaslots(kvm);
	printk(KERN_INFO "A- slots->used_slots = [%d]\n", slots->used_slots);
	
	struct kvm_page_pol act = app.tab[0];
	printk(KERN_INFO "AU Départ gpn = [%lu] et politique = {%c}", act.gpn, act.pol);
	
	j = 0; k = 0; i = 0; n = app.taille;
	
	if (slots->used_slots != 0){
		
		while(i < n){
			act =  app.tab[i];

			//operations start here on act
		
			printk(KERN_INFO "structure i = (%d) de gpn [%lu] et de politique {%c} .",i,act.gpn,act.pol);
			while (j < slots->used_slots) {
				//printk(KERN_INFO "PREMIER WHILE j = %u",j);
				slot = &slots->memslots[j];
				N = slot->npages;
				long list_gpn[N];
				while (k < N) {
					//printk(KERN_INFO "DEUXIEME WHILE j= %u, k = %u",j,k);
					info = &slot->vfn_dsm_state[k];
		
					list_gpn[k] = __kvm_dsm_vfn_to_gfn(slot, false,slot->base_vfn + k, 0, NULL);
					//printk(KERN_INFO "PREMIER IF j= %u, k = %u",j,k);
					if (list_gpn[k] == act.gpn){
							printk(KERN_INFO "pour le slot j = (%d) et le npages - %d - , match (%lu)",j,k,act.gpn);
							info->policy = act.pol;
				
							printk(KERN_INFO "\tgfn\tpol\n");
							printk(KERN_INFO "\t[%llu]; \t %c;",list_gpn[k], info->policy);
							printk(KERN_INFO "***************");
							break;
							}
					k = k + 1;
					//printk(KERN_INFO "FIN IF j= %u, k = %u",j,k);
					}
				//printk(KERN_INFO "FIN DEUXIEME WHILE");
				if (k < N) break;
				j = j + 1 ;
				//printk(KERN_INFO "FIN DEUXIEME IF");
			}
			i =i+1;;
		}
		
		/*kfree(slots);
		kfree(slot);
		kfree(info);*/
	}
	
}
