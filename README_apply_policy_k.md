# QEMU-KVM NOTIFICATION MECHANISM 
*apply_policy branch*

## Presentation
  In this branch of the project, we want to implement a mechanism that allows the user-space processes managed by QEMU to send information to the Kernel-space KVM module. In this README, we focused on the **Linux host** part. The shared information describes a special protocol to be used on the given pages. The entries in the file should have the following two columns: one describing the GPN, the other describing the protocol to be applied.
  

## Implementation 

We implement our ioctl in the host as in the userspace QEMU. That work is done in the  kvm.h file in Linux-DSM/include/uapi/linux directory.
Our work in this part is divided into 02 steps.

4. **Ioctl is captured by KVM** 
 Vm system calls can be captured in this following function which can be found in the folder /Linux-DSM/virt/kvm
 
`static long kvm_vm_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)`

5. **The structure that describes the status of pages in the DSM is updated to reflect the new policy**.
These operations are performed in the function that manipulates the variable-size array that we received :
`void kvm_apply_policy(struct kvm *kvm, page_pol_t app)`.
This is done by this operation : `info->policy = act.pol;`

Policies can therefore be applied on the pages concerned.



	

