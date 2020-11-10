# IMPLEMENTATION OF A WRITE_UPDATE PROTOCOL NAMED ATOMIC_INC

## Content

  The purpose of this file is to present how the **ATOMIC_INC** protocol is implemented in this kernel according to the following chronology:
  - Sending the hypercall
  - Reception of the hypercall
  - Local update (method 2)
  - Propagation of the update
  - Remote update
  
  ## Context and work
  Here, we have to implement the protocol named **ATOMIC_INC** based on the write update to make a unit increment. This protocol is used to make the synchronization of some variables that are simple and need a fairly flexible and simple means of synchronization.
  This protocol can be used for any variable which is just a simple variable that can be passed through and hypercall.
  The prerequisites to be able to run this module are the same as those to run the distributed virtual machine.
  
  ## Steps
  This work was done in several stages, which have been mentioned above, and which we will detail in the following.
  As part of our work, we manipulated the variable **Jiffies**, which is used for the internal clock of the system.
  Here is how the process works.
  
  ### The Hypercall
   The hypercall should be done inside the guest VM. 
   
   The first thing was to define the Hypercall number inside the guest and the host. in the two kernels, we have found the file : 
   *include/uapi/linux/kvm_para.h*.
   then inserted the line
   
    `#define KVM_HC_ATOMIC_INC  		10`
   
  which describes the hypercall number which will be used between the two kernels for communication. 
  the next step was modifying the internal clock for it to send the hypercall.
   We have found the function which holds the incrementation of the jiffies variable. it is named **do_timer** we can find it in the guest Repository here : https://github.com/moharaka/giantvm_guest/tree/write_update; in the file *kernel/time/timekeeping.c* and added some informations. we have sent the hypercall with some parameters which are : 
   - the gpa of the jiffies variable,
   - the jiffies size
   - the jiffies variable
   - the ticks value
  ### Hypercall capture
  We catch the hypercall in the host, in this branch or in the *write_update_send_all* branch
  the parameters we have sent in the previous step, are the same that we recieve in this step.
  the reception is done in a function called **kvm_emulate_hypercall(struct kvm_vcpu *vcpu)**  defined in a file named : *arch/x86/kvm/x86.c* 
  then we call the function **int send_upd_request(struct kvm *kvm, phys_addr_t var_gpa, long var_size,unsigned long var_datas)** in a case described with our hypercall number. this function can do twho things depending on the method we choose to continue.
  ### Local update (method 2)
  This step is thus in case we choose the second method between these two methods below : 
  - **Send all the updates then update in every machine** this method, is used to send the updates to all the machines (even to the message sender) such that every machine will catch it and update the jiffies locally. 
  - **Update jiffies locally then send the updates to the others nodes**  this method, is just for us to update locally the variable (jiffies in this case), then, send the updates to the other nodes.
  These two methods have been implemented but only the first method works. despite that, taken alone, every function works.
  
  from this step, and in the next, all the modifications have been done in the *arch/x86/kvm/ivy.c* file. 
  In this step, we update the jiffies locally in the function **int update_local(struct kvm *kvm,bool is_smm,gfn_t gfn,unsigned long gpa,long size,unsigned long var_data)** that we call in the function **send_upd_request**. before the updates propagation.
  
  We should notice that : 
  - The second method is implemented in this branch. and the first method (without this step) is implemented in the *write_update_send_all* branch
  - The first method takes care of the fact that a node should be able to send a request to himself, and also allow himself to recieve a request from himself. (these modifications have been done in the **int ivy_kvm_dsm_handle_req(void *data)** function and in the **send_upd_req**)  
  ### Propagation of the update
  We have sent a request to the nodes (all the nodes in the first method to the other nodes in the second method) to ask them to update the variable locally.
  then we reach the last step.
  ### Remote update
  The last but not the least step, was the handling of the requests. this is done by the function : **static int dsm_handle_send_upd_req(struct kvm *kvm, kconnection_t *conn_sock, struct kvm_memory_slot *memslot, struct kvm_dsm_memory_slot *slot, const struct dsm_request *req, bool *retry, hfn_t vfn, char *page,tx_add_t *tx_add)**. it is called in the **int ivy_kvm_dsm_handle_req(void *data)** function, in the WRITE_UPDATE case.
  
  This function, writes in the memory, the u to date value of the variable. then the cycle restarts.
  
  
  There are the steps in the synchronization process of the jiffies variable in the distributed virtual machine.
