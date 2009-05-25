/* irq_init.c
 *
 *  This file contains the implementation of rtems initialization
 *  related to interrupt handling.
 *
 *  CopyRight (C) 1999 valette@crf.canon.fr
 *
 * Enhanced by Jay Kulpinski <jskulpin@eng01.gdds.com>
 * to make it valid for MVME2300 Motorola boards.
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.OARcorp.com/rtems/license.html.
 *
 *  $Id: irq_init.c,v 1.2 2001-09-27 12:01:06 chris Exp $
 */
#include <bsp/consoleIo.h>
#include <libcpu/io.h>
#include <libcpu/spr.h>
#include <bsp/pci.h>
#include <bsp/residual.h>
#include <bsp/openpic.h>
#include <bsp/irq.h>
#include <bsp.h>
#include <libcpu/raw_exception.h>
#include <bsp/motorola.h>

/*
#define SHOW_ISA_PCI_BRIDGE_SETTINGS
*/

typedef struct {
  unsigned char bus;	/* few chance the PCI/ISA bridge is not on first bus but ... */
  unsigned char device;
  unsigned char function;
} pci_isa_bridge_device;

pci_isa_bridge_device* via_82c586 = 0;
static pci_isa_bridge_device bridge;

extern unsigned int external_exception_vector_prolog_code_size;
extern void external_exception_vector_prolog_code();
extern unsigned int decrementer_exception_vector_prolog_code_size;
extern void decrementer_exception_vector_prolog_code();

/*
 * default on/off function
 */
static void nop_func(){}
/*
 * default isOn function
 */
static int not_connected() {return 0;}
/*
 * default possible isOn function
 */
static int connected() {return 1;}

static rtems_irq_connect_data     	rtemsIrq[BSP_IRQ_NUMBER];
static rtems_irq_global_settings     	initial_config;
static rtems_irq_connect_data     	defaultIrq = {
  /* vectorIdex,	 hdl		, on		, off		, isOn */
  0, 			 nop_func	, nop_func	, nop_func	, not_connected
};
static rtems_irq_prio irqPrioTable[BSP_IRQ_NUMBER]={
  /*
   * actual rpiorities for interrupt :
   *	0   means that only current interrupt is masked
   *	255 means all other interrupts are masked
   */
  /*
   * ISA interrupts.
   * The second entry has a priority of 255 because
   * it is the slave pic entry and is should always remain
   * unmasked.
   */
  0,0,
  255,
  0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  /*
   * PCI Interrupts
   */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* for raven prio 0 means unactive... */
  /*
   * Processor exceptions handled as interrupts
   */
  0
};

static unsigned char mcp750_openpic_initsenses[] = {
    1,	/* MCP750_INT_PCB(8259) */
    0,	/* MCP750_INT_FALCON_ECC_ERR */
    1,	/* MCP750_INT_PCI_ETHERNET */
    1,	/* MCP750_INT_PCI_PMC */
    1,	/* MCP750_INT_PCI_WATCHDOG_TIMER1 */
    1,	/* MCP750_INT_PCI_PRST_SIGNAL */
    1,	/* MCP750_INT_PCI_FALL_SIGNAL */
    1,	/* MCP750_INT_PCI_DEG_SIGNAL */
    1,	/* MCP750_INT_PCI_BUS1_INTA */
    1,	/* MCP750_INT_PCI_BUS1_INTB */
    1,	/* MCP750_INT_PCI_BUS1_INTC */
    1,	/* MCP750_INT_PCI_BUS1_INTD */
    1,	/* MCP750_INT_PCI_BUS2_INTA */
    1,	/* MCP750_INT_PCI_BUS2_INTB */
    1,	/* MCP750_INT_PCI_BUS2_INTC */
    1,	/* MCP750_INT_PCI_BUS2_INTD */
};

void VIA_isa_bridge_interrupts_setup(void)
{
  pci_isa_bridge_device pci_dev;
  unsigned int temp;
  unsigned char tmp;
  unsigned char maxBus;
  unsigned found = 0;

  maxBus = BusCountPCI();
  pci_dev.function 	= 0; /* Assumes the bidge is the first function */
     
  for (pci_dev.bus = 0; pci_dev.bus < maxBus; pci_dev.bus++) {
#ifdef SCAN_PCI_PRINT       
    printk("isa_bridge_interrupts_setup: Scanning bus %d\n", pci_dev.bus);
#endif       
    for (pci_dev.device = 0; pci_dev.device < PCI_MAX_DEVICES; pci_dev.device++) {
#ifdef SCAN_PCI_PRINT       
      printk("isa_bridge_interrupts_setup: Scanning device %d\n", pci_dev.device);
#endif       
      pci_read_config_dword(pci_dev.bus, pci_dev.device,  pci_dev.function,
			       PCI_VENDOR_ID, &temp);
#ifdef SCAN_PCI_PRINT       
      printk("Vendor/device = %x\n", temp);
#endif
      if ((temp == (((unsigned short) PCI_VENDOR_ID_VIA) | (PCI_DEVICE_ID_VIA_82C586_0 << 16)))
	 ) {
	bridge = pci_dev;
	via_82c586 = &bridge;
#ifdef SHOW_ISA_PCI_BRIDGE_SETTINGS	
	/*
	 * Should print : bus = 0, device = 11, function = 0 on a MCP750.
	 */
	printk("Via PCI/ISA bridge found at bus = %d, device = %d, function = %d\n",
	       via_82c586->bus,
	       via_82c586->device,
	       via_82c586->function);
#endif       
	found = 1;
	goto loop_exit;
	   
      }
    }
  }
loop_exit:
  if (!found) BSP_panic("VIA_82C586 PCI/ISA bridge not found!n");
     
  tmp = inb(0x810);
  if  ( !(tmp & 0x2)) {
#ifdef SHOW_ISA_PCI_BRIDGE_SETTINGS	
    printk("This is a second generation MCP750 board\n");
    printk("We must reprogram the PCI/ISA bridge...\n");
#endif       
    pci_read_config_byte(via_82c586->bus, via_82c586->device, via_82c586->function,
			 0x47,  &tmp);
#ifdef SHOW_ISA_PCI_BRIDGE_SETTINGS	
    printk(" PCI ISA bridge control2 = %x\n", (unsigned) tmp);
#endif       
    /*
     * Enable 4D0/4D1 ISA interrupt level/edge config registers
     */
    tmp |= 0x20;
    pci_write_config_byte(via_82c586->bus, via_82c586->device, via_82c586->function,
			  0x47, tmp);
    /*
     * Now program the ISA interrupt edge/level
     */
    tmp = ELCRS_INT9_LVL | ELCRS_INT10_LVL | ELCRS_INT11_LVL;
    outb(tmp, ISA8259_S_ELCR);
    tmp = ELCRM_INT5_LVL;
    outb(tmp, ISA8259_M_ELCR);;
    /*
     * Set the Interrupt inputs to non-inverting level interrupt
     */
    pci_read_config_byte(via_82c586->bus, via_82c586->device, via_82c586->function,
			    0x54, &tmp);
#ifdef SHOW_ISA_PCI_BRIDGE_SETTINGS	
    printk(" PCI ISA bridge PCI/IRQ Edge/Level Select = %x\n", (unsigned) tmp);
#endif       
    tmp = 0;
    pci_write_config_byte(via_82c586->bus, via_82c586->device, via_82c586->function,
			  0x54, tmp);
  }
  else {
#ifdef SHOW_ISA_PCI_BRIDGE_SETTINGS	
    printk("This is a first generation MCP750 board\n");
    printk("We just show the actual value used by PCI/ISA bridge\n");
#endif       
    pci_read_config_byte(via_82c586->bus, via_82c586->device, via_82c586->function,
			 0x47,  &tmp);
#ifdef SHOW_ISA_PCI_BRIDGE_SETTINGS	
    printk(" PCI ISA bridge control2 = %x\n", (unsigned) tmp);
#endif       
    /*
     * Show the Interrupt inputs inverting/non-inverting level status
     */
    pci_read_config_byte(via_82c586->bus, via_82c586->device, via_82c586->function,
			 0x54, &tmp);
#ifdef SHOW_ISA_PCI_BRIDGE_SETTINGS	
    printk(" PCI ISA bridge PCI/IRQ Edge/Level Select = %x\n", (unsigned) tmp);
#endif       
  }
}

  /*
   * This code assumes the exceptions management setup has already
   * been done. We just need to replace the exceptions that will
   * be handled like interrupt. On mcp750/mpc750 and many PPC processors
   * this means the decrementer exception and the external exception.
   */
void BSP_rtems_irq_mng_init(unsigned cpuId)
{
  rtems_raw_except_connect_data vectorDesc;
  int known_cpi_isa_bridge = 0;
  int i;
  
  /*
   * First initialize the Interrupt management hardware
   */
  OpenPIC_InitSenses = mcp750_openpic_initsenses;
  OpenPIC_NumInitSenses = sizeof(mcp750_openpic_initsenses) / sizeof(char);
#ifdef TRACE_IRQ_INIT  
  printk("Going to initialize raven interrupt controller (openpic compliant)\n");
#endif       
  openpic_init(1);
#ifdef TRACE_IRQ_INIT  
  printk("Going to initialize the PCI/ISA bridge IRQ related setting (VIA 82C586)\n");
#endif
  if ( currentBoard == MESQUITE ) {
    VIA_isa_bridge_interrupts_setup();
    known_cpi_isa_bridge = 1;
  }
  if ( currentBoard == MVME_2300 ) {
    /* nothing to do for W83C553 bridge */
    known_cpi_isa_bridge = 1;
  }
  if (!known_cpi_isa_bridge) {
    printk("Please add code for PCI/ISA bridge init to libbsp/shared/irq/irq_init.c\n");
    printk("If your card works correctly please add a test and set known_cpi_isa_bridge to true\n");
  }
#ifdef TRACE_IRQ_INIT  
  printk("Going to initialize the ISA PC legacy IRQ management hardware\n");
#endif       
  BSP_i8259s_init();
  /*
   * Initialize Rtems management interrupt table
   */
    /*
     * re-init the rtemsIrq table
     */
    for (i = 0; i < BSP_IRQ_NUMBER; i++) {
      rtemsIrq[i]      = defaultIrq;
      rtemsIrq[i].name = i;
    }
    /*
     * Init initial Interrupt management config
     */
    initial_config.irqNb 	= BSP_IRQ_NUMBER;
    initial_config.defaultEntry = defaultIrq;
    initial_config.irqHdlTbl	= rtemsIrq;
    initial_config.irqBase	= BSP_ASM_IRQ_VECTOR_BASE;
    initial_config.irqPrioTbl	= irqPrioTable;

    if (!BSP_rtems_irq_mngt_set(&initial_config)) {
      /*
       * put something here that will show the failure...
       */
      BSP_panic("Unable to initialize RTEMS interrupt Management!!! System locked\n");
    }
  
  /*
   * We must connect the raw irq handler for the two
   * expected interrupt sources : decrementer and external interrupts.
   */
    vectorDesc.exceptIndex 	=	ASM_DEC_VECTOR;
    vectorDesc.hdl.vector	=	ASM_DEC_VECTOR;
    vectorDesc.hdl.raw_hdl	=	decrementer_exception_vector_prolog_code;
    vectorDesc.hdl.raw_hdl_size	=	(unsigned) &decrementer_exception_vector_prolog_code_size;
    vectorDesc.on		=	nop_func;
    vectorDesc.off		=	nop_func;
    vectorDesc.isOn		=	connected;
    if (!mpc60x_set_exception (&vectorDesc)) {
      BSP_panic("Unable to initialize RTEMS decrementer raw exception\n");
    }
    vectorDesc.exceptIndex	=	ASM_EXT_VECTOR;
    vectorDesc.hdl.vector	=	ASM_EXT_VECTOR;
    vectorDesc.hdl.raw_hdl	=	external_exception_vector_prolog_code;
    vectorDesc.hdl.raw_hdl_size	=	(unsigned) &external_exception_vector_prolog_code_size;
    if (!mpc60x_set_exception (&vectorDesc)) {
      BSP_panic("Unable to initialize RTEMS external raw exception\n");
    }
#ifdef TRACE_IRQ_INIT  
    printk("RTEMS IRQ management is now operationnal\n");
#endif
}

