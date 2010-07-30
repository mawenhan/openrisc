/* ethernet.c -- Simulation of Ethernet MAC

   Copyright (C) 2001 by Erez Volk, erez@opencores.org
                         Ivan Guzvinec, ivang@opencores.org
   Copyright (C) 2008 Embecosm Limited

   Contributor Jeremy Bennett <jeremy.bennett@embecosm.com>

   This file is part of Or1ksim, the OpenRISC 1000 Architectural Simulator.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 3 of the License, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   You should have received a copy of the GNU General Public License along
   with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* This program is commented throughout in a fashion suitable for processing
   with Doxygen. */


/* Autoconf and/or portability configuration */
#include "config.h"
#include "port.h"

/* System includes */
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>

/* Package includes */
#include "arch.h"
#include "config.h"
#include "abstract.h"
#include "eth.h"
#include "dma.h"
#include "sim-config.h"
#include "fields.h"
#include "crc32.h"
#include "vapi.h"
#include "pic.h"
#include "sched.h"
#include "toplevel-support.h"
#include "sim-cmd.h"

struct eth_device
{
  /* Is peripheral enabled */
  int enabled;

  /* Base address in memory */
  oraddr_t baseaddr;

  /* Which DMA controller is this MAC connected to */
  unsigned dma;
  unsigned tx_channel;
  unsigned rx_channel;

  /* Our address */
  unsigned char mac_address[ETHER_ADDR_LEN];

  /* interrupt line */
  unsigned long mac_int;

  /* VAPI ID */
  unsigned long base_vapi_id;

  /* RX and TX file names and handles */
  char *rxfile, *txfile;
  int txfd;
  int rxfd;
  off_t loopback_offset;

  /* Socket interface name */
  char *sockif;

  int rtx_sock;
  int rtx_type;
  struct ifreq ifr;
  fd_set rfds, wfds;

  /* Current TX state */
  struct
  {
    unsigned long state;
    unsigned long bd_index;
    unsigned long bd;
    unsigned long bd_addr;
    unsigned working, waiting_for_dma, error;
    long packet_length;
    unsigned minimum_length, maximum_length;
    unsigned add_crc;
    unsigned crc_dly;
    unsigned long crc_value;
    long bytes_left, bytes_sent;
  } tx;

  /* Current RX state */
  struct
  {
    unsigned long state;
    unsigned long bd_index;
    unsigned long bd;
    unsigned long bd_addr;
    int fd;
    off_t *offset;
    unsigned working, error, waiting_for_dma;
    long packet_length, bytes_read, bytes_left;
  } rx;

  /* Visible registers */
  struct
  {
    unsigned long moder;
    unsigned long int_source;
    unsigned long int_mask;
    unsigned long ipgt;
    unsigned long ipgr1;
    unsigned long ipgr2;
    unsigned long packetlen;
    unsigned long collconf;
    unsigned long tx_bd_num;
    unsigned long controlmoder;
    unsigned long miimoder;
    unsigned long miicommand;
    unsigned long miiaddress;
    unsigned long miitx_data;
    unsigned long miirx_data;
    unsigned long miistatus;
    unsigned long hash0;
    unsigned long hash1;

    /* Buffer descriptors */
    unsigned long bd_ram[ETH_BD_SPACE / 4];
  } regs;

  unsigned char rx_buff[ETH_MAXPL];
  unsigned char tx_buff[ETH_MAXPL];
  unsigned char lo_buff[ETH_MAXPL];
};


/* simulator interface */
static void eth_vapi_read (unsigned long id, unsigned long data, void *dat);
/* register interface */
static void eth_write32 (oraddr_t addr, uint32_t value, void *dat);
static uint32_t eth_read32 (oraddr_t addr, void *dat);
/* clock */
static void eth_controller_tx_clock (void *);
static void eth_controller_rx_clock (void *);
/* utility functions */
static ssize_t eth_read_rx_file (struct eth_device *, void *, size_t);
static void eth_skip_rx_file (struct eth_device *, off_t);
static void eth_rx_next_packet (struct eth_device *);
static void eth_write_tx_bd_num (struct eth_device *, unsigned long value);
/* ========================================================================= */
/*  TX LOGIC                                                                 */
/*---------------------------------------------------------------------------*/

/*
 * TX clock
 * Responsible for starting and finishing TX
 */
static void
eth_controller_tx_clock (void *dat)
{
  struct eth_device *eth = dat;
  int bAdvance = 1;
#if HAVE_ETH_PHY
  struct sockaddr_ll sll;
#endif /* HAVE_ETH_PHY */
  long nwritten = 0;
  unsigned long read_word;

  switch (eth->tx.state)
    {
    case ETH_TXSTATE_IDLE:
      eth->tx.state = ETH_TXSTATE_WAIT4BD;
      break;
    case ETH_TXSTATE_WAIT4BD:
      /* Read buffer descriptor */
      eth->tx.bd = eth->regs.bd_ram[eth->tx.bd_index];
      eth->tx.bd_addr = eth->regs.bd_ram[eth->tx.bd_index + 1];

      if (TEST_FLAG (eth->tx.bd, ETH_TX_BD, READY))
	{
	    /*****************/
	  /* initialize TX */
	  eth->tx.bytes_left = eth->tx.packet_length =
	    GET_FIELD (eth->tx.bd, ETH_TX_BD, LENGTH);
	  eth->tx.bytes_sent = 0;

	  /*   Initialize error status bits */
	  CLEAR_FLAG (eth->tx.bd, ETH_TX_BD, DEFER);
	  CLEAR_FLAG (eth->tx.bd, ETH_TX_BD, COLLISION);
	  CLEAR_FLAG (eth->tx.bd, ETH_TX_BD, RETRANSMIT);
	  CLEAR_FLAG (eth->tx.bd, ETH_TX_BD, UNDERRUN);
	  CLEAR_FLAG (eth->tx.bd, ETH_TX_BD, NO_CARRIER);
	  SET_FIELD (eth->tx.bd, ETH_TX_BD, RETRY, 0);

	  /* Find out minimum length */
	  if (TEST_FLAG (eth->tx.bd, ETH_TX_BD, PAD) ||
	      TEST_FLAG (eth->regs.moder, ETH_MODER, PAD))
	    eth->tx.minimum_length =
	      GET_FIELD (eth->regs.packetlen, ETH_PACKETLEN, MINFL);
	  else
	    eth->tx.minimum_length = eth->tx.packet_length;

	  /* Find out maximum length */
	  if (TEST_FLAG (eth->regs.moder, ETH_MODER, HUGEN))
	    eth->tx.maximum_length = eth->tx.packet_length;
	  else
	    eth->tx.maximum_length =
	      GET_FIELD (eth->regs.packetlen, ETH_PACKETLEN, MAXFL);

	  /* Do we need CRC on this packet? */
	  if (TEST_FLAG (eth->regs.moder, ETH_MODER, CRCEN) ||
	      (TEST_FLAG (eth->tx.bd, ETH_TX_BD, CRC) &&
	       TEST_FLAG (eth->tx.bd, ETH_TX_BD, LAST)))
	    eth->tx.add_crc = 1;
	  else
	    eth->tx.add_crc = 0;

	  if (TEST_FLAG (eth->regs.moder, ETH_MODER, DLYCRCEN))
	    eth->tx.crc_dly = 1;
	  else
	    eth->tx.crc_dly = 0;
	  /* XXX - For now we skip CRC calculation */

	  if (eth->rtx_type == ETH_RTX_FILE)
	    {
	      /* write packet length to file */
	      nwritten =
		write (eth->txfd, &(eth->tx.packet_length),
		       sizeof (eth->tx.packet_length));
	    }

	    /************************************************/
	  /* start transmit with reading packet into FIFO */
	  eth->tx.state = ETH_TXSTATE_READFIFO;
	}

      /* stay in this state if (TXEN && !READY) */
      break;
    case ETH_TXSTATE_READFIFO:
#if 1
      if (eth->tx.bytes_sent < eth->tx.packet_length)
	{
	  read_word =
	    eval_direct32 (eth->tx.bytes_sent + eth->tx.bd_addr, 0, 0);
	  eth->tx_buff[eth->tx.bytes_sent] =
	    (unsigned char) (read_word >> 24);
	  eth->tx_buff[eth->tx.bytes_sent + 1] =
	    (unsigned char) (read_word >> 16);
	  eth->tx_buff[eth->tx.bytes_sent + 2] =
	    (unsigned char) (read_word >> 8);
	  eth->tx_buff[eth->tx.bytes_sent + 3] = (unsigned char) (read_word);
	  eth->tx.bytes_sent += 4;
	}
#else
      if (eth->tx.bytes_sent < eth->tx.packet_length)
	{
	  eth->tx_buff[eth->tx.bytes_sent] =
	    eval_direct8 (eth->tx.bytes_sent + eth->tx.bd_addr, 0, 0);
	  eth->tx.bytes_sent += 1;
	}
#endif
      else
	{
	  eth->tx.state = ETH_TXSTATE_TRANSMIT;
	}
      break;
    case ETH_TXSTATE_TRANSMIT:
      /* send packet */
      switch (eth->rtx_type)
	{
	case ETH_RTX_FILE:
	  nwritten = write (eth->txfd, eth->tx_buff, eth->tx.packet_length);
	  break;
#if HAVE_ETH_PHY
	case ETH_RTX_SOCK:
	  memset (&sll, 0, sizeof (sll));
	  sll.sll_ifindex = eth->ifr.ifr_ifindex;
	  nwritten =
	    sendto (eth->rtx_sock, eth->tx_buff, eth->tx.packet_length, 0,
		    (struct sockaddr *) &sll, sizeof (sll));
#endif /* HAVE_ETH_PHY */
	}

      /* set BD status */
      if (nwritten == eth->tx.packet_length)
	{
	  CLEAR_FLAG (eth->tx.bd, ETH_TX_BD, READY);
	  SET_FLAG (eth->regs.int_source, ETH_INT_SOURCE, TXB);

	  eth->tx.state = ETH_TXSTATE_WAIT4BD;
	}
      else
	{
	  /* XXX - implement retry mechanism here! */
	  CLEAR_FLAG (eth->tx.bd, ETH_TX_BD, READY);
	  CLEAR_FLAG (eth->tx.bd, ETH_TX_BD, COLLISION);
	  SET_FLAG (eth->regs.int_source, ETH_INT_SOURCE, TXE);

	  eth->tx.state = ETH_TXSTATE_WAIT4BD;
	}

      eth->regs.bd_ram[eth->tx.bd_index] = eth->tx.bd;

      /* generate OK interrupt */
      if (TEST_FLAG (eth->regs.int_mask, ETH_INT_MASK, TXE_M) ||
	  TEST_FLAG (eth->regs.int_mask, ETH_INT_MASK, TXB_M))
	{
	  if (TEST_FLAG (eth->tx.bd, ETH_TX_BD, IRQ))
	    report_interrupt (eth->mac_int);
	}

      /* advance to next BD */
      if (bAdvance)
	{
	  if (TEST_FLAG (eth->tx.bd, ETH_TX_BD, WRAP) ||
	      eth->tx.bd_index >= ETH_BD_COUNT)
	    eth->tx.bd_index = 0;
	  else
	    eth->tx.bd_index += 2;
	}

      break;
    }

  /* Reschedule */
  SCHED_ADD (eth_controller_tx_clock, dat, 1);
}

/* ========================================================================= */


/* ========================================================================= */
/*  RX LOGIC                                                                 */
/*---------------------------------------------------------------------------*/

/*
 * RX clock
 * Responsible for starting and finishing RX
 */
static void
eth_controller_rx_clock (void *dat)
{
  struct eth_device *eth = dat;
  long nread;
  unsigned long send_word;


  switch (eth->rx.state)
    {
    case ETH_RXSTATE_IDLE:
      eth->rx.state = ETH_RXSTATE_WAIT4BD;
      break;

    case ETH_RXSTATE_WAIT4BD:
      eth->rx.bd = eth->regs.bd_ram[eth->rx.bd_index];
      eth->rx.bd_addr = eth->regs.bd_ram[eth->rx.bd_index + 1];

      if (TEST_FLAG (eth->rx.bd, ETH_RX_BD, READY))
	{
	    /*****************/
	  /* Initialize RX */
	  CLEAR_FLAG (eth->rx.bd, ETH_RX_BD, MISS);
	  CLEAR_FLAG (eth->rx.bd, ETH_RX_BD, INVALID);
	  CLEAR_FLAG (eth->rx.bd, ETH_RX_BD, DRIBBLE);
	  CLEAR_FLAG (eth->rx.bd, ETH_RX_BD, UVERRUN);
	  CLEAR_FLAG (eth->rx.bd, ETH_RX_BD, COLLISION);
	  CLEAR_FLAG (eth->rx.bd, ETH_RX_BD, TOOBIG);
	  CLEAR_FLAG (eth->rx.bd, ETH_RX_BD, TOOSHORT);

	  /* Setup file to read from */
	  if (TEST_FLAG (eth->regs.moder, ETH_MODER, LOOPBCK))
	    {
	      eth->rx.fd = eth->txfd;
	      eth->rx.offset = &(eth->loopback_offset);
	    }
	  else
	    {
	      eth->rx.fd = eth->rxfd;
	      eth->rx.offset = 0;
	    }
	  eth->rx.state = ETH_RXSTATE_RECV;
	}
      else if (!TEST_FLAG (eth->regs.moder, ETH_MODER, RXEN))
	{
	  eth->rx.state = ETH_RXSTATE_IDLE;
	}
      else
	{
	  nread =
	    recv (eth->rtx_sock, eth->rx_buff, ETH_MAXPL, /*MSG_PEEK | */
		  MSG_DONTWAIT);
	  if (nread > 0)
	    {
	      SET_FLAG (eth->regs.int_source, ETH_INT_SOURCE, BUSY);
	      if (TEST_FLAG (eth->regs.int_mask, ETH_INT_MASK, BUSY_M))
		report_interrupt (eth->mac_int);
	    }
	}
      break;

    case ETH_RXSTATE_RECV:
      switch (eth->rtx_type)
	{
	case ETH_RTX_FILE:
	  /* Read packet length */
	  if (eth_read_rx_file
	      (eth, &(eth->rx.packet_length),
	       sizeof (eth->rx.packet_length)) <
	      sizeof (eth->rx.packet_length))
	    {
	      /* TODO: just do what real ethernet would do (some kind of error state) */
	      sim_done ();
	      break;
	    }

	  /* Packet must be big enough to hold a header */
	  if (eth->rx.packet_length < ETHER_HDR_LEN)
	    {
	      eth_rx_next_packet (eth);

	      eth->rx.state = ETH_RXSTATE_WAIT4BD;
	      break;
	    }

	  eth->rx.bytes_read = 0;
	  eth->rx.bytes_left = eth->rx.packet_length;

	  /* for now Read entire packet into memory */
	  nread = eth_read_rx_file (eth, eth->rx_buff, eth->rx.bytes_left);
	  if (nread < eth->rx.bytes_left)
	    {
	      eth->rx.error = 1;
	      break;
	    }

	  eth->rx.packet_length = nread;
	  eth->rx.bytes_left = nread;
	  eth->rx.bytes_read = 0;

	  eth->rx.state = ETH_RXSTATE_WRITEFIFO;

	  break;

	case ETH_RTX_SOCK:
	  nread = recv (eth->rtx_sock, eth->rx_buff, ETH_MAXPL, MSG_DONTWAIT);

	  if (nread == 0)
	    {
	      break;
	    }
	  else if (nread < 0)
	    {
	      if (errno != EAGAIN)
		{
		  break;
		}
	      else
		break;
	    }
	  /* If not promiscouos mode, check the destination address */
	  if (!TEST_FLAG (eth->regs.moder, ETH_MODER, PRO))
	    {
	      if (TEST_FLAG (eth->regs.moder, ETH_MODER, IAM)
		  && (eth->rx_buff[0] & 1))
		{
		  /* Nothing for now */
		}

	      if (eth->mac_address[5] != eth->rx_buff[0] ||
		  eth->mac_address[4] != eth->rx_buff[1] ||
		  eth->mac_address[3] != eth->rx_buff[2] ||
		  eth->mac_address[2] != eth->rx_buff[3] ||
		  eth->mac_address[1] != eth->rx_buff[4] ||
		  eth->mac_address[0] != eth->rx_buff[5])
		break;
	    }

	  eth->rx.packet_length = nread;
	  eth->rx.bytes_left = nread;
	  eth->rx.bytes_read = 0;

	  eth->rx.state = ETH_RXSTATE_WRITEFIFO;

	  break;
	case ETH_RTX_VAPI:
	  break;
	}
      break;

    case ETH_RXSTATE_WRITEFIFO:
#if 1
      send_word = ((unsigned long) eth->rx_buff[eth->rx.bytes_read] << 24) |
	((unsigned long) eth->rx_buff[eth->rx.bytes_read + 1] << 16) |
	((unsigned long) eth->rx_buff[eth->rx.bytes_read + 2] << 8) |
	((unsigned long) eth->rx_buff[eth->rx.bytes_read + 3]);
      set_direct32 (eth->rx.bd_addr + eth->rx.bytes_read, send_word, 0, 0);
      /* update counters */
      eth->rx.bytes_left -= 4;
      eth->rx.bytes_read += 4;
#else
      set_direct8 (eth->rx.bd_addr + eth->rx.bytes_read,
		   eth->rx_buff[eth->rx.bytes_read], 0, 0);
      eth->rx.bytes_left -= 1;
      eth->rx.bytes_read += 1;
#endif

      if (eth->rx.bytes_left <= 0)
	{
	  /* Write result to bd */
	  SET_FIELD (eth->rx.bd, ETH_RX_BD, LENGTH, eth->rx.packet_length);
	  CLEAR_FLAG (eth->rx.bd, ETH_RX_BD, READY);
	  SET_FLAG (eth->regs.int_source, ETH_INT_SOURCE, RXB);

	  if (eth->rx.packet_length <
	      (GET_FIELD (eth->regs.packetlen, ETH_PACKETLEN, MINFL) - 4))
	    SET_FLAG (eth->rx.bd, ETH_RX_BD, TOOSHORT);
	  if (eth->rx.packet_length >
	      GET_FIELD (eth->regs.packetlen, ETH_PACKETLEN, MAXFL))
	    SET_FLAG (eth->rx.bd, ETH_RX_BD, TOOBIG);

	  eth->regs.bd_ram[eth->rx.bd_index] = eth->rx.bd;

	  /* advance to next BD */
	  if (TEST_FLAG (eth->rx.bd, ETH_RX_BD, WRAP)
	      || eth->rx.bd_index >= ETH_BD_COUNT)
	    eth->rx.bd_index = eth->regs.tx_bd_num << 1;
	  else
	    eth->rx.bd_index += 2;

	  if ((TEST_FLAG (eth->regs.int_mask, ETH_INT_MASK, RXB_M)) &&
	      (TEST_FLAG (eth->rx.bd, ETH_RX_BD, IRQ)))
	    {
	      report_interrupt (eth->mac_int);
	    }

	  /* ready to receive next packet */
	  eth->rx.state = ETH_RXSTATE_IDLE;
	}
      break;
    }

  /* Reschedule */
  SCHED_ADD (eth_controller_rx_clock, dat, 1);
}

/* ========================================================================= */
/* Move to next RX BD */
static void
eth_rx_next_packet (struct eth_device *eth)
{
  /* Skip any possible leftovers */
  if (eth->rx.bytes_left)
    eth_skip_rx_file (eth, eth->rx.bytes_left);
}

/* "Skip" bytes in RX file */
static void
eth_skip_rx_file (struct eth_device *eth, off_t count)
{
  eth->rx.offset += count;
}

/*
 * Utility function to read from the ethernet RX file
 * This function moves the file pointer to the current place in the packet before reading
 */
static ssize_t
eth_read_rx_file (struct eth_device *eth, void *buf, size_t count)
{
  ssize_t result;

  if (eth->rx.fd <= 0)
    {
      return 0;
    }

  if (eth->rx.offset)
    if (lseek (eth->rx.fd, *(eth->rx.offset), SEEK_SET) == (off_t) - 1)
      {
	return 0;
      }

  result = read (eth->rx.fd, buf, count);
  if (eth->rx.offset && result >= 0)
    *(eth->rx.offset) += result;

  return result;
}

/* ========================================================================= */

/*
  Reset. Initializes all registers to default and places devices in
         memory address space.
*/
static void
eth_reset (void *dat)
{
  struct eth_device *eth = dat;
#if HAVE_ETH_PHY
  int j;
  struct sockaddr_ll sll;
#endif /* HAVE_ETH_PHY */

  if (eth->baseaddr != 0)
    {
      switch (eth->rtx_type)
	{
	case ETH_RTX_FILE:
	  /* (Re-)open TX/RX files */
	  if (eth->rxfd > 0)
	    close (eth->rxfd);
	  if (eth->txfd > 0)
	    close (eth->txfd);
	  eth->rxfd = eth->txfd = -1;

	  if ((eth->rxfd = open (eth->rxfile, O_RDONLY)) < 0)
	    fprintf (stderr, "Cannot open Ethernet RX file \"%s\"\n",
		     eth->rxfile);
	  if ((eth->txfd = open (eth->txfile, O_RDWR | O_CREAT | O_APPEND
#if defined(O_SYNC)		/* BSD / Mac OS X manual doesn't know about O_SYNC */
				 | O_SYNC
#endif
				 ,
				 S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0)
	    fprintf (stderr, "Cannot open Ethernet TX file \"%s\"\n",
		     eth->txfile);
	  eth->loopback_offset = lseek (eth->txfd, 0, SEEK_END);

	  break;
#if HAVE_ETH_PHY
	case ETH_RTX_SOCK:
	  /* (Re-)open TX/RX sockets */
	  if (eth->rtx_sock != 0)
	    break;

	  eth->rtx_sock = socket (PF_PACKET, SOCK_RAW, htons (ETH_P_ALL));
	  if (eth->rtx_sock == -1)
	    {
	      fprintf (stderr, "Cannot open rtx_sock.\n");
	      return;
	    }

	  /* get interface index number */
	  memset (&(eth->ifr), 0, sizeof (eth->ifr));
	  strncpy (eth->ifr.ifr_name, eth->sockif, IFNAMSIZ);
	  if (ioctl (eth->rtx_sock, SIOCGIFINDEX, &(eth->ifr)) == -1)
	    {
	      fprintf (stderr, "SIOCGIFINDEX failed!\n");
	      return;
	    }

	  /* Bind to interface... */
	  memset (&sll, 0xff, sizeof (sll));
	  sll.sll_family = AF_PACKET;	/* allways AF_PACKET */
	  sll.sll_protocol = htons (ETH_P_ALL);
	  sll.sll_ifindex = eth->ifr.ifr_ifindex;
	  if (bind (eth->rtx_sock, (struct sockaddr *) &sll, sizeof (sll)) ==
	      -1)
	    {
	      fprintf (stderr, "Error bind().\n");
	      return;
	    }

	  /* first, flush all received packets. */
	  do
	    {
	      fd_set fds;
	      struct timeval t;

	      FD_ZERO (&fds);
	      FD_SET (eth->rtx_sock, &fds);
	      memset (&t, 0, sizeof (t));
	      j = select (FD_SETSIZE, &fds, NULL, NULL, &t);
	      if (j > 0)
		recv (eth->rtx_sock, eth->rx_buff, j, 0);
	    }
	  while (j);

	  break;
#else /* HAVE_ETH_PHY */
	case ETH_RTX_SOCK:
	  fprintf (stderr,
		   "Ethernet phy not enabled in this configuration.  Configure with --enable-ethphy.\n");
	  exit (1);
	  break;
#endif /* HAVE_ETH_PHY */
	}

      /* Set registers to default values */
      memset (&(eth->regs), 0, sizeof (eth->regs));
      eth->regs.moder = 0x0000A000;
      eth->regs.ipgt = 0x00000012;
      eth->regs.ipgr1 = 0x0000000C;
      eth->regs.ipgr2 = 0x00000012;
      eth->regs.packetlen = 0x003C0600;
      eth->regs.collconf = 0x000F003F;
      eth->regs.miimoder = 0x00000064;
      eth->regs.tx_bd_num = 0x00000040;

      /* Initialize TX/RX status */
      memset (&(eth->tx), 0, sizeof (eth->tx));
      memset (&(eth->rx), 0, sizeof (eth->rx));
      eth->rx.bd_index = eth->regs.tx_bd_num << 1;

      /* Initialize VAPI */
      if (eth->base_vapi_id)
	{
	  vapi_install_multi_handler (eth->base_vapi_id, ETH_NUM_VAPI_IDS,
				      eth_vapi_read, dat);
	}
    }
}

/* ========================================================================= */


/* 
  Print register values on stdout 
*/
static void
eth_status (void *dat)
{
  struct eth_device *eth = dat;

  PRINTF ("\nEthernet MAC at 0x%" PRIxADDR ":\n", eth->baseaddr);
  PRINTF ("MODER        : 0x%08lX\n", eth->regs.moder);
  PRINTF ("INT_SOURCE   : 0x%08lX\n", eth->regs.int_source);
  PRINTF ("INT_MASK     : 0x%08lX\n", eth->regs.int_mask);
  PRINTF ("IPGT         : 0x%08lX\n", eth->regs.ipgt);
  PRINTF ("IPGR1        : 0x%08lX\n", eth->regs.ipgr1);
  PRINTF ("IPGR2        : 0x%08lX\n", eth->regs.ipgr2);
  PRINTF ("PACKETLEN    : 0x%08lX\n", eth->regs.packetlen);
  PRINTF ("COLLCONF     : 0x%08lX\n", eth->regs.collconf);
  PRINTF ("TX_BD_NUM    : 0x%08lX\n", eth->regs.tx_bd_num);
  PRINTF ("CTRLMODER    : 0x%08lX\n", eth->regs.controlmoder);
  PRINTF ("MIIMODER     : 0x%08lX\n", eth->regs.miimoder);
  PRINTF ("MIICOMMAND   : 0x%08lX\n", eth->regs.miicommand);
  PRINTF ("MIIADDRESS   : 0x%08lX\n", eth->regs.miiaddress);
  PRINTF ("MIITX_DATA   : 0x%08lX\n", eth->regs.miitx_data);
  PRINTF ("MIIRX_DATA   : 0x%08lX\n", eth->regs.miirx_data);
  PRINTF ("MIISTATUS    : 0x%08lX\n", eth->regs.miistatus);
  PRINTF ("MAC Address  : %02X:%02X:%02X:%02X:%02X:%02X\n",
	  eth->mac_address[0], eth->mac_address[1], eth->mac_address[2],
	  eth->mac_address[3], eth->mac_address[4], eth->mac_address[5]);
  PRINTF ("HASH0        : 0x%08lX\n", eth->regs.hash0);
  PRINTF ("HASH1        : 0x%08lX\n", eth->regs.hash1);
}

/* ========================================================================= */


/* 
  Read a register 
*/
static uint32_t
eth_read32 (oraddr_t addr, void *dat)
{
  struct eth_device *eth = dat;

  switch (addr)
    {
    case ETH_MODER:
      return eth->regs.moder;
    case ETH_INT_SOURCE:
      return eth->regs.int_source;
    case ETH_INT_MASK:
      return eth->regs.int_mask;
    case ETH_IPGT:
      return eth->regs.ipgt;
    case ETH_IPGR1:
      return eth->regs.ipgr1;
    case ETH_IPGR2:
      return eth->regs.ipgr2;
    case ETH_PACKETLEN:
      return eth->regs.packetlen;
    case ETH_COLLCONF:
      return eth->regs.collconf;
    case ETH_TX_BD_NUM:
      return eth->regs.tx_bd_num;
    case ETH_CTRLMODER:
      return eth->regs.controlmoder;
    case ETH_MIIMODER:
      return eth->regs.miimoder;
    case ETH_MIICOMMAND:
      return eth->regs.miicommand;
    case ETH_MIIADDRESS:
      return eth->regs.miiaddress;
    case ETH_MIITX_DATA:
      return eth->regs.miitx_data;
    case ETH_MIIRX_DATA:
      return eth->regs.miirx_data;
    case ETH_MIISTATUS:
      return eth->regs.miistatus;
    case ETH_MAC_ADDR0:
      return (((unsigned long) eth->mac_address[3]) << 24) |
	(((unsigned long) eth->mac_address[2]) << 16) |
	(((unsigned long) eth->mac_address[1]) << 8) |
	(unsigned long) eth->mac_address[0];
    case ETH_MAC_ADDR1:
      return (((unsigned long) eth->mac_address[5]) << 8) |
	(unsigned long) eth->mac_address[4];
    case ETH_HASH0:
      return eth->regs.hash0;
    case ETH_HASH1:
      return eth->regs.hash1;
      /*case ETH_DMA_RX_TX: return eth_rx( eth ); */
    }

  if ((addr >= ETH_BD_BASE) && (addr < ETH_BD_BASE + ETH_BD_SPACE))
    return eth->regs.bd_ram[(addr - ETH_BD_BASE) / 4];

  PRINTF ("eth_read32( 0x%" PRIxADDR " ): Illegal address\n",
	  addr + eth->baseaddr);
  return 0;
}

/* ========================================================================= */


/* 
  Write a register 
*/
static void
eth_write32 (oraddr_t addr, uint32_t value, void *dat)
{
  struct eth_device *eth = dat;

  switch (addr)
    {
    case ETH_MODER:

      if (!TEST_FLAG (eth->regs.moder, ETH_MODER, RXEN) &&
	  TEST_FLAG (value, ETH_MODER, RXEN))
	SCHED_ADD (eth_controller_rx_clock, dat, 1);
      else if (!TEST_FLAG (value, ETH_MODER, RXEN))
	SCHED_FIND_REMOVE (eth_controller_rx_clock, dat);

      if (!TEST_FLAG (eth->regs.moder, ETH_MODER, TXEN) &&
	  TEST_FLAG (value, ETH_MODER, TXEN))
	SCHED_ADD (eth_controller_tx_clock, dat, 1);
      else if (!TEST_FLAG (value, ETH_MODER, TXEN))
	SCHED_FIND_REMOVE (eth_controller_tx_clock, dat);

      eth->regs.moder = value;

      if (TEST_FLAG (value, ETH_MODER, RST))
	eth_reset (dat);
      return;
    case ETH_INT_SOURCE:
      if (!(eth->regs.int_source & ~value) && eth->regs.int_source)
	clear_interrupt (eth->mac_int);
      eth->regs.int_source &= ~value;
      return;
    case ETH_INT_MASK:
      eth->regs.int_mask = value;
      return;
    case ETH_IPGT:
      eth->regs.ipgt = value;
      return;
    case ETH_IPGR1:
      eth->regs.ipgr1 = value;
      return;
    case ETH_IPGR2:
      eth->regs.ipgr2 = value;
      return;
    case ETH_PACKETLEN:
      eth->regs.packetlen = value;
      return;
    case ETH_COLLCONF:
      eth->regs.collconf = value;
      return;
    case ETH_TX_BD_NUM:
      eth_write_tx_bd_num (eth, value);
      return;
    case ETH_CTRLMODER:
      eth->regs.controlmoder = value;
      return;
    case ETH_MIIMODER:
      eth->regs.miimoder = value;
      return;
    case ETH_MIICOMMAND:
      eth->regs.miicommand = value;
      return;
    case ETH_MIIADDRESS:
      eth->regs.miiaddress = value;
      return;
    case ETH_MIITX_DATA:
      eth->regs.miitx_data = value;
      return;
    case ETH_MIIRX_DATA:
      eth->regs.miirx_data = value;
      return;
    case ETH_MIISTATUS:
      eth->regs.miistatus = value;
      return;
    case ETH_MAC_ADDR0:
      eth->mac_address[0] = value & 0xFF;
      eth->mac_address[1] = (value >> 8) & 0xFF;
      eth->mac_address[2] = (value >> 16) & 0xFF;
      eth->mac_address[3] = (value >> 24) & 0xFF;
      return;
    case ETH_MAC_ADDR1:
      eth->mac_address[4] = value & 0xFF;
      eth->mac_address[5] = (value >> 8) & 0xFF;
      return;
    case ETH_HASH0:
      eth->regs.hash0 = value;
      return;
    case ETH_HASH1:
      eth->regs.hash1 = value;
      return;

      /*case ETH_DMA_RX_TX: eth_tx( eth, value ); return; */
    }

  if ((addr >= ETH_BD_BASE) && (addr < ETH_BD_BASE + ETH_BD_SPACE))
    {
      eth->regs.bd_ram[(addr - ETH_BD_BASE) / 4] = value;
      return;
    }

  PRINTF ("eth_write32( 0x%" PRIxADDR " ): Illegal address\n",
	  addr + eth->baseaddr);
  return;
}

/* ========================================================================= */


/*
 *   VAPI connection to outside
 */
static void
eth_vapi_read (unsigned long id, unsigned long data, void *dat)
{
  unsigned long which;
  struct eth_device *eth = dat;

  which = id - eth->base_vapi_id;

  if (!eth)
    {
      return;
    }

  switch (which)
    {
    case ETH_VAPI_DATA:
      break;
    case ETH_VAPI_CTRL:
      break;
    }
}

/* ========================================================================= */


/* When TX_BD_NUM is written, also reset current RX BD index */
static void
eth_write_tx_bd_num (struct eth_device *eth, unsigned long value)
{
  eth->regs.tx_bd_num = value & 0xFF;
  eth->rx.bd_index = eth->regs.tx_bd_num << 1;
}

/* ========================================================================= */

/*-----------------------------------------------[ Ethernet configuration ]---*/


/*---------------------------------------------------------------------------*/
/*!Enable or disable the Ethernet interface

   @param[in] val  The value to use
   @param[in] dat  The config data structure                                 */
/*---------------------------------------------------------------------------*/
static void
eth_enabled (union param_val  val,
	     void            *dat)
{
  struct eth_device *eth = dat;

  eth->enabled = val.int_val;

}	/* eth_enabled() */


/*---------------------------------------------------------------------------*/
/*!Set the Ethernet interface base address

   @param[in] val  The value to use
   @param[in] dat  The config data structure                                 */
/*---------------------------------------------------------------------------*/
static void
eth_baseaddr (union param_val  val,
	      void            *dat)
{
  struct eth_device *eth = dat;

  eth->baseaddr = val.addr_val;

}	/* eth_baseaddr() */


/*---------------------------------------------------------------------------*/
/*!Set the Ethernet DMA port

   This is not currently supported, so a warning message is printed.

   @param[in] val  The value to use
   @param[in] dat  The config data structure                                 */
/*---------------------------------------------------------------------------*/
static void
eth_dma (union param_val  val,
	 void            *dat)
{
  struct eth_device *eth = dat;

  fprintf (stderr, "Warning: External Ethernet DMA not currently supported\n");
  eth->dma = val.addr_val;

}	/* eth_dma() */


/*---------------------------------------------------------------------------*/
/*!Set the Ethernet IRQ

   @param[in] val  The value to use
   @param[in] dat  The config data structure                                 */
/*---------------------------------------------------------------------------*/
static void
eth_irq (union param_val  val,
	 void            *dat)
{
  struct eth_device *eth = dat;

  eth->mac_int = val.int_val;

}	/* eth_irq() */


/*---------------------------------------------------------------------------*/
/*!Set the Ethernet interface type

   Currently two types are supported, file and socket. Use of the socket
   requires a compile time option.

   @param[in] val  The value to use. 0 for file, 1 for socket.
   @param[in] dat  The config data structure                                 */
/*---------------------------------------------------------------------------*/
static void
eth_rtx_type (union param_val  val,
	      void            *dat)
{
  struct eth_device *eth = dat;

  if (val.int_val)
    {
#ifndef HAVE_ETH_PHY
      fprintf (stderr, "Warning: Ethernet PHY socket not enabled in this "
	       "configuration (configure with --enable-ethphy): ignored\n");
      return;
#endif
    }

  eth->rtx_type = val.int_val;

}	/* eth_rtx_type() */


/*---------------------------------------------------------------------------*/
/*!Set the Ethernet DMA Rx channel

   External DMA is not currently supported, so a warning message is printed.

   @param[in] val  The value to use
   @param[in] dat  The config data structure                                 */
/*---------------------------------------------------------------------------*/
static void
eth_rx_channel (union param_val  val,
		void            *dat)
{
  struct eth_device *eth = dat;

  fprintf (stderr, "Warning: External Ethernet DMA not currently supported: "
	   "Rx channel ignored\n");
  eth->rx_channel = val.int_val;

}	/* eth_rx_channel() */


/*---------------------------------------------------------------------------*/
/*!Set the Ethernet DMA Tx channel

   External DMA is not currently supported, so a warning message is printed.

   @param[in] val  The value to use
   @param[in] dat  The config data structure                                 */
/*---------------------------------------------------------------------------*/
static void
eth_tx_channel (union param_val  val,
		void            *dat)
{
  struct eth_device *eth = dat;

  fprintf (stderr, "Warning: External Ethernet DMA not currently supported: "
	   "Tx channel ignored\n");
  eth->tx_channel = val.int_val;

}	/* eth_tx_channel() */


/*---------------------------------------------------------------------------*/
/*!Set the Ethernet DMA Rx file

   Free any previously allocated value.

   @param[in] val  The value to use
   @param[in] dat  The config data structure                                 */
/*---------------------------------------------------------------------------*/
static void
eth_rxfile (union param_val  val,
	    void            *dat)
{
  struct eth_device *eth = dat;

  if (NULL != eth->rxfile)
    {
      free (eth->rxfile);
      eth->rxfile = NULL;
    }

  if (!(eth->rxfile = strdup (val.str_val)))
    {
      fprintf (stderr, "Peripheral Ethernet: Run out of memory\n");
      exit (-1);
    }
}	/* eth_rxfile() */


/*---------------------------------------------------------------------------*/
/*!Set the Ethernet DMA Tx file

   Free any previously allocated value.

   @param[in] val  The value to use
   @param[in] dat  The config data structure                                 */
/*---------------------------------------------------------------------------*/
static void
eth_txfile (union param_val  val,
	    void            *dat)
{
  struct eth_device *eth = dat;

  if (NULL != eth->txfile)
    {
      free (eth->txfile);
      eth->txfile = NULL;
    }

  if (!(eth->txfile = strdup (val.str_val)))
    {
      fprintf (stderr, "Peripheral Ethernet: Run out of memory\n");
      exit (-1);
    }
}	/* eth_txfile() */


/*---------------------------------------------------------------------------*/
/*!Set the Ethernet socket interface

   Free any previously allocated value. This is only meaningful if the socket
   interface is configured.

   @param[in] val  The value to use
   @param[in] dat  The config data structure                                 */
/*---------------------------------------------------------------------------*/
static void
eth_sockif (union param_val  val,
	    void            *dat)
{
  struct eth_device *eth = dat;

#ifndef HAVE_ETH_PHY
  fprintf (stderr, "Warning: Ethernet PHY socket not enabled in this "
	   "configuration (configure with --enable-ethphy): "
	   "sockif ignored\n");
  return;
#endif

  if (NULL != eth->sockif)
    {
      free (eth->sockif);
      eth->sockif = NULL;
    }

  if (!(eth->sockif = strdup (val.str_val)))
    {
      fprintf (stderr, "Peripheral Ethernet: Run out of memory\n");
      exit (-1);
    }
}	/* eth_sockif() */


static void
eth_vapi_id (union param_val  val,
	     void            *dat)
{
  struct eth_device *eth = dat;
  eth->base_vapi_id = val.int_val;
}

/*---------------------------------------------------------------------------*/
/*!Initialize a new Ethernet configuration

   ALL parameters are set explicitly to default values.                      */
/*---------------------------------------------------------------------------*/
static void *
eth_sec_start (void)
{
  struct eth_device *new = malloc (sizeof (struct eth_device));

  if (!new)
    {
      fprintf (stderr, "Peripheral Eth: Run out of memory\n");
      exit (-1);
    }

  memset (new, 0, sizeof (struct eth_device));

  new->enabled      = 1;
  new->baseaddr     = 0;
  new->dma          = 0;
  new->mac_int      = 0;
  new->rtx_type     = 0;
  new->rx_channel   = 0;
  new->tx_channel   = 0;
  new->rxfile       = strdup ("eth_rx");
  new->txfile       = strdup ("eth_tx");
  new->sockif       = strdup ("or1ksim_eth");
  new->base_vapi_id = 0;

  return new;
}

static void
eth_sec_end (void *dat)
{
  struct eth_device *eth = dat;
  struct mem_ops ops;

  if (!eth->enabled)
    {
      free (eth->rxfile);
      free (eth->txfile);
      free (eth->sockif);
      free (eth);
      return;
    }

  memset (&ops, 0, sizeof (struct mem_ops));

  ops.readfunc32 = eth_read32;
  ops.writefunc32 = eth_write32;
  ops.read_dat32 = dat;
  ops.write_dat32 = dat;

  /* FIXME: Correct delay? */
  ops.delayr = 2;
  ops.delayw = 2;
  reg_mem_area (eth->baseaddr, ETH_ADDR_SPACE, 0, &ops);
  reg_sim_stat (eth_status, dat);
  reg_sim_reset (eth_reset, dat);
}


/*---------------------------------------------------------------------------*/
/*!Register a new Ethernet configuration                                     */
/*---------------------------------------------------------------------------*/
void
reg_ethernet_sec ()
{
  struct config_section *sec =
    reg_config_sec ("ethernet", eth_sec_start, eth_sec_end);

  reg_config_param (sec, "enabled",    PARAMT_INT,  eth_enabled);
  reg_config_param (sec, "baseaddr",   PARAMT_ADDR, eth_baseaddr);
  reg_config_param (sec, "dma",        PARAMT_INT,  eth_dma);
  reg_config_param (sec, "irq",        PARAMT_INT,  eth_irq);
  reg_config_param (sec, "rtx_type",   PARAMT_INT,  eth_rtx_type);
  reg_config_param (sec, "rx_channel", PARAMT_INT,  eth_rx_channel);
  reg_config_param (sec, "tx_channel", PARAMT_INT,  eth_tx_channel);
  reg_config_param (sec, "rxfile",     PARAMT_STR,  eth_rxfile);
  reg_config_param (sec, "txfile",     PARAMT_STR,  eth_txfile);
  reg_config_param (sec, "sockif",     PARAMT_STR,  eth_sockif);
  reg_config_param (sec, "vapi_id",    PARAMT_INT,  eth_vapi_id);

}	/* reg_ethernet_sec() */

