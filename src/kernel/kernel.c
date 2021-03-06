#include <fb.h>
#include <hw.h>
#include <string.h>
#include <mem.h>
#include <rtc.h>
#include <pic.h>
#include <serial.h>
#include <kb.h>
#include <errors.h>
#include <devices.h>
#include <vfs.h>
#include <fs/rootfs.h>
<<<<<<< HEAD
#include <proc.h>
#include <syscall.h>
=======
#include <time.h>
>>>>>>> projects/time

/* Just the declaration of the second, main kernel routine. */
void kmain2();

void kmain(void *gdt_base, void *mem_map) {
  /* This is a hack. We need to set the stack to the right place, but we need
   * to do this AFTER the memory allocator has checked and initialized the
   * memory. To do this we'll manipulate the stack in a dangerous way, but
   * since we won't return from this function its somehow safe. Also, to keep
   * things simple, this function should have no activation registry - or at
   * least that's what I think, since I can't be sure what will GCC do.
   * In any case, after relocating the stack we'll just call kmain_stage_2
   * so there should be no need for anything in the stack since the address
   * to kmain2 must be statically computed. */

  /* Luckily, framebuffer driver is basically static, except for fb_printf.
   * If we have to print anything here let it feel TERRIBLE. */
  fb_reset();

  /* Initialize the memory. We're using the stack set during the real-mode
   * initial steps. */
  if (mem_setup(gdt_base, mem_map) == -1) {
    kernel_panic("Could not initialize memory :(");
  }
  /* Our stack will be 4K long situated at the end of the kernel space, right
   * before the user space. We need to allocate this very frame. */
  if (mem_allocate_frames(1,
                          MEM_KERNEL_STACK_FRAME,
                          MEM_USER_FIRST_FRAME) == NULL) {
    kernel_panic("Could not allocate a frame for the kernel's stack :(");
  }

  /* And now comes the magic. Fingers crossed. */
  mem_relocate_stack_to((void *)MEM_KERNEL_STACK_TOP);

  /* At this point, we are standing in the new stack. We can continue but we
   * can't return here. */
  kmain2();
}

void kmain2() {
  vfs_file_t *f;

  #include "../userland/tests/build/hello.h"

  /* Now we're here, let's set the panic level to hysterical: nothing here
   * can fail. */
  set_panic_level(PANIC_HYSTERICAL);

  /* Set up the interrupt subsytem. */
  itr_set_up();

  /* Initialize the Virtual File System. */
  vfs_init();

  /* Intializes the rootfs. */
  rootfs_init();

  /* Mount rootfs on "/" */
  vfs_mount(ROOTFS_DEVID, "/", ROOTFS_NAME);

  /* Initializes the dev subsystem. */
  dev_init();

  set_panic_level(PANIC_PERROR);

  /* Complete memory initialization now as a device and filesystem module. */
  mem_init();

<<<<<<< HEAD
=======
  memset(buf2, '-', 10);
  f = vfs_open("/dev/zero", FILE_O_READ, 0);
  if (f == NULL) kernel_panic("no /dev/zero\n");
  if (vfs_read(f, buf2 + 3, 5) != 5) kernel_panic("read failed.\n");
  fb_write(buf2, 10);
   


>>>>>>> projects/time
  /* Initializes the PICs. This mask all interrupts. */
  pic_init();

  /* Activate the keyboard. */
  kb_init();
  pic_unmask_dev(PIC_KEYBOARD_IRQ);

  /* Start serial. */
  serial_init();
  pic_unmask_dev(PIC_SERIAL_1_IRQ);
  pic_unmask_dev(PIC_SERIAL_2_IRQ);

  /* Start system calls subsystem. */
  syscall_init();

  hw_sti();

  /* Test userland. */

  f = vfs_open("/init", FILE_O_WRITE | FILE_O_CREATE, 0755);
  if (f == NULL) kernel_panic("no /init\n");
  vfs_write(f, tests_build_hello, tests_build_hello_len);
  vfs_close(f);

  proc_init();

  proc_exec("/init");

    

    /*
  RTC DRIVERS
  */  
  rtc_init();
  //testing get_time()
  struct tm mytm;
  time_get(&mytm);
  fb_printf("\nTHE TIME IS:\n");
  time_show(&mytm);
    
  //testing set_time()
  struct tm mytm2;
  mytm2.hours = 18;
  mytm2.seconds = 59;
  mytm2.minutes = 59;
  mytm2.day = 31;
  mytm2.month = 12;
  mytm2.year = 2100;
  time_set(&mytm2);

  
  struct tm mytm3;
  time_get(&mytm3); 
  
  fb_printf("\n\nTHE SET TIME IS:\n");
  time_show(&mytm3);

  
  //testing sleep()
  struct tm mytm5;
  time_get(&mytm5);
  fb_printf("\nTHE CURRENT TIME IS:\n");
  time_show(&mytm5);
  
  time_sleep(5);
  fb_printf("\nAfter sleep\n");

  struct tm mytm4;
  time_get(&mytm4);
  fb_printf("\nTHE CURRENT TIME IS:\n");
  time_show(&mytm4);





  /* This is the idle loop. */
  while (1) {
    hw_hlt();
  }
}
