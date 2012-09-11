/*
 *  linux/drivers/video/eink/broadsheet/broadsheet_mxc.c --
 *  eInk frame buffer device HAL broadsheet hw
 *
 *      Copyright (C) 2005-2009 Amazon Technologies
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <asm/arch/ipu.h>

#undef USE_BS_IRQ
#ifdef USE_BS_IRQ
#include <asm/irq.h>
#include <asm/arch/irqs.h>
#include <asm/mach/irq.h>
#endif

extern int  broadsheet_gpio_config(irq_handler_t broadsheet_irq_handler, char *broadsheet_irq_handler_name);
extern void broadsheet_gpio_disable(int disable_bs_irq);
extern int  broadsheet_ready(void);
extern void broadsheet_reset(void);

#include "../hal/einkfb_hal.h"
#include "broadsheet.h"

#if PRAGMAS
    #pragma mark Definitions/Globals
#endif

#define BROADSHEET_GPIO_INIT_SUCCESS    0
#define BROADSHEET_HIRQ_RQST_FAILURE    1
#define BROADSHEET_HIRQ_INIT_FAILURE    2
#define BROADSHEET_HRST_INIT_FAILURE    3
#define BROADSHEET_HRDY_INIT_FAILURE    4

#define DISABLE_BS_IRQ                  1
#define NO_BS_IRQ                       0

#define BROADSHEET_DISPLAY_NUMBER       DISP0
#define BROADSHEET_PIXEL_FORMAT         IPU_PIX_FMT_RGB565
#define BROADSHEET_SCREEN_HEIGHT        825
#define BROADSHEET_SCREEN_WIDTH         1200
#define BROADSHEET_SCREEN_BPP           4
#define BROADSHEET_READY_TIMEOUT        BS_RDY_TIMEOUT
#define BROADSHEET_DMA_TIMEOUT          BS_DMA_TIMEOUT
#define BROADSHEET_DMA_MIN_SIZE         64
#define BROADSHEET_DMA_IN_USE           1
#define BROADSHEET_DMA_DONE             0

#define BROADSHEET_DMA_SIZE(s)          BROADSHEET_DMA_MIN_SIZE,     \
                                        (s)/BROADSHEET_DMA_MIN_SIZE, \
                                        BROADSHEET_DMA_MIN_SIZE

// Select which R/W timing parameters to use (slow or fast); these
// settings affect the Freescale IPU Asynchronous Parallel System 80
// Interface (Type 2) timings.
// The slow timing parameters are meant to be used for bring-up
// and debugging the functionality.  The fast timings are up to the
// Broadsheet specifications for the 16-bit Host Interface Timing, and
// are meant to maximize performance.
//
#undef SLOW_RW_TIMING

#ifdef SLOW_RW_TIMING // Slow timing for bring-up or debugging
#    define BROADSHEET_READ_CYCLE_TIME     1900  // nsec
#    define BROADSHEET_READ_UP_TIME         170  // nsec
#    define BROADSHEET_READ_DOWN_TIME      1040  // nsec
#    define BROADSHEET_READ_LATCH_TIME     1900  // nsec
#    define BROADSHEET_PIXEL_CLK        5000000
#    define BROADSHEET_WRITE_CYCLE_TIME    1230  // nsec
#    define BROADSHEET_WRITE_UP_TIME        170  // nsec
#    define BROADSHEET_WRITE_DOWN_TIME      680  // nsec
#else // Fast timing, according to Broadsheet specs 
// NOTE: these timings assume a Broadsheet System Clock at the 
//       maximum speed of 66MHz (15.15 nsec period) 
#    define BROADSHEET_READ_CYCLE_TIME      110  // nsec 
#    define BROADSHEET_READ_UP_TIME           1  // nsec 
#    define BROADSHEET_READ_DOWN_TIME       100  // nsec 
#    define BROADSHEET_READ_LATCH_TIME      110  // nsec 
#    define BROADSHEET_PIXEL_CLK        5000000 
#    define BROADSHEET_WRITE_CYCLE_TIME      83  // nsec 
#    define BROADSHEET_WRITE_UP_TIME          1  // nsec 
#    define BROADSHEET_WRITE_DOWN_TIME       72  // nsec 
#endif

static void       bs_eof_irq_completion(void);

static uint16_t   broadsheet_screen_height = BROADSHEET_SCREEN_HEIGHT;
static uint16_t   broadsheet_screen_width  = BROADSHEET_SCREEN_WIDTH;

static uint32_t   broadsheet_screen_stride = BPP_SIZE(BROADSHEET_SCREEN_WIDTH, BROADSHEET_SCREEN_BPP);

static bool       dma_available = false;
static bool       dma_xfer_done = true;
static uint32_t   dma_started;
static uint32_t   dma_stopped;

#if PRAGMAS
    #pragma mark -
    #pragma mark Broadsheet HW Primitives
    #pragma mark -
#endif

/*
 * Wait until the HRDY line of the Broadsheet controller is asserted.
 */

static bool bs_hardware_ready(void *unused)
{
    return ( !broadsheet_force_hw_not_ready() && broadsheet_ready() );
}

static int wait_for_ready(void)
{
    return ( EINKFB_SCHEDULE_TIMEOUT(BROADSHEET_READY_TIMEOUT, bs_hardware_ready) );
}

bool bs_hrdy_preflight(void)
{
    return ( EINKFB_SUCCESS == wait_for_ready() );
}

#define BS_READY()      (broadsheet_ignore_hw_ready() || (EINKFB_SUCCESS == wait_for_ready()))
#define USE_WR_BUF(s)   (BS_CMD_ARGS_MAX < (s))
#define USE_RD_BUF(s)   (BS_RD_DAT_ONE != (s))
#define BS_CMD          true
#define BS_DAT          false
#define BS_WR           true
#define BS_RD           false

static int bs_wr_which(bool which, uint32_t data)
{
    display_port_t disp = BROADSHEET_DISPLAY_NUMBER;
    cmddata_t type = (BS_CMD == which) ? CMD : DAT;
    int result = EINKFB_FAILURE;
    
    if ( BS_READY() )
        result = ipu_adc_write_cmd(disp, type, data, 0, 0);
    else
        result = EINKFB_FAILURE;
    
    return ( result );
}

int bs_wr_cmd(bs_cmd cmd, bool poll)
{
    int result;
    
    einkfb_debug_full("command = 0x%04X, poll = %d\n", cmd, poll);
    result = bs_wr_which(BS_CMD, (uint32_t)cmd);
    
    if ( (EINKFB_SUCCESS == result) && poll )
    {
        if ( !BS_READY() )
           result = EINKFB_FAILURE; 
    }

    return ( result );
}

// Scheduled loop for doing IO on framebuffer-sized buffers.
//
static int bs_io_buf(u32 data_size, u16 *data, bool which)
{
    display_port_t disp = BROADSHEET_DISPLAY_NUMBER;
    int result = EINKFB_FAILURE;

    einkfb_debug_full("size    = %d\n", data_size);

    if ( BS_READY() )
    {
        int     i = 0, j, length = (EINKFB_MEMCPY_MIN >> 1), num_loops = data_size/length,
                remainder = data_size % length;
        bool    done = false;
        
        if ( 0 != num_loops )
            einkfb_debug("num_loops @ %d bytes = %d, remainder = %d\n",
                (length << 1), num_loops, (remainder << 1));
        
        result = EINKFB_SUCCESS;
        
        // Read/write EINKFB_MEMCPY_MIN bytes (hence, divide by 2) at a time.  While
        // there are still bytes to read/write, yield the CPU.
        //
        do
        {
            if ( 0 >= num_loops )
                length = remainder;

            for ( j = 0; j < length; j++)
            {
                if ( BS_WR == which )
                    ipu_adc_write_cmd(disp, DAT, (uint32_t)data[i + j], 0, 0);
                else
                    data[i + j] = (u16)(ipu_adc_read_data(disp) & 0x0000FFFF);
            }
                
            i += length;
            
            if ( i < data_size )
            {
                EINKFB_SCHEDULE();
                num_loops--;
            }
            else
                done = true;
        }
        while ( !done );
    }

    return ( result );
}

static bool bs_dma_ready(void *unused)
{
    return ( dma_xfer_done );
}

static int bs_io_buf_dma(u32 data_size, dma_addr_t phys_addr)
{
    int result = EINKFB_FAILURE;

    if ( BS_READY() )
    {
        // Keep PIO mode fast until we need DMA.
        //
        ipu_adc_set_dma_in_use(BROADSHEET_DMA_IN_USE);
    
        // Set up for the DMA transfer.
        //
        if ( EINKFB_SUCCESS == ipu_init_channel_buffer(ADC_SYS1, IPU_INPUT_BUFFER, BROADSHEET_PIXEL_FORMAT,
             BROADSHEET_DMA_SIZE(data_size), IPU_ROTATE_NONE, phys_addr, 0, 0, 0) )
        {
            ipu_enable_irq(IPU_IRQ_ADC_SYS1_EOF);
            ipu_enable_channel(ADC_SYS1);
            dma_xfer_done = false;
            dma_started = jiffies;
            
            // Start it.
            //
            ipu_select_buffer(ADC_SYS1, IPU_INPUT_BUFFER, 0);
            einkfb_debug("DMA transfer started\n");
            
            // Wait for it to complete.
            //
            result = EINKFB_SCHEDULE_TIMEOUT(BROADSHEET_DMA_TIMEOUT, bs_dma_ready);
            
            // If it fails to complete, complete it anyway.
            //
            if ( EINKFB_FAILURE == result )
                bs_eof_irq_completion();
        }
        
        // Done with DMA for now, so make PIO fast again.
        //
        ipu_adc_set_dma_in_use(BROADSHEET_DMA_DONE);
    }
    
    return ( result );
}

bool bs_wr_one_ready(void)
{
    bool ready = false;
    
    if ( BS_READY() )
        ready = true;
    
    return ( ready );
}

void bs_wr_one(u16 data)
{
    ipu_adc_write_cmd((display_port_t)BROADSHEET_DISPLAY_NUMBER, DAT, (uint32_t)data, 0, 0);
}

int bs_wr_dat(bool which, u32 data_size, u16 *data)
{
    int result = EINKFB_FAILURE;
    
    if ( USE_WR_BUF(data_size) )
    {
        u16 *pio_data = data;
        
        if ( dma_available && broadsheet_needs_dma() )
        {
            dma_addr_t phys_addr = bs_get_phys_addr();

            if ( phys_addr )
            {
                u32 alignment = 0, align_size = 0, remainder = 0;
                
                // Ensure that we're doing enough DMA to warrant it.
                //
                if ( BROADSHEET_DMA_MIN_SIZE <= data_size )
                {
                    alignment  = data_size / BROADSHEET_DMA_MIN_SIZE;
                    
                    // Ensure that the DMA we're doing is properly aligned.
                    //
                    align_size = alignment * BROADSHEET_DMA_MIN_SIZE;
                    remainder  = data_size % BROADSHEET_DMA_MIN_SIZE;
                }
                
                einkfb_debug("DMA alignment = %d: align_size = %d, remainder = %d\n",
                    alignment, align_size, remainder);

                // Send the properly sized and aligned data along to be DMA'd.
                //
                if ( align_size )
                    result = bs_io_buf_dma(align_size, phys_addr);
                
                // If the DMA went well, only PIO the remainder if there is any.
                //
                if ( EINKFB_SUCCESS == result )
                {
                    data_size = remainder;
                    
                    if ( remainder )
                    {
                        pio_data = &data[alignment * BROADSHEET_DMA_MIN_SIZE];
                        result = EINKFB_FAILURE;
                    }
                }

                bs_clear_phys_addr();
            }
        }

        if ( EINKFB_FAILURE == result )
            result = bs_io_buf(data_size, pio_data, BS_WR);
    }
    else
    {
        int i; result = EINKFB_SUCCESS;
        
        for ( i = 0; (i < data_size) && (EINKFB_SUCCESS == result); i++ )
        {
            if ( BS_WR_DAT_DATA == which )
            {
                if ( EINKFB_DEBUG() && (9 > i) )
                    einkfb_debug_full("data[%d] = 0x%04X\n", i, data[i]);
            }
            else
                einkfb_debug_full("args[%d] = 0x%04X\n", i, data[i]);
            
            result = bs_wr_which(BS_DAT, (uint32_t)data[i]);
        }
    }
    
    return ( result );
}

static int bs_rd_one(u16 *data)
{
    display_port_t disp = BROADSHEET_DISPLAY_NUMBER;
    int result = EINKFB_SUCCESS;
    
    if ( BS_READY() )
        *data = (u16)(ipu_adc_read_data(disp) & 0x0000FFFF);
    else
        result = EINKFB_FAILURE;
    
    if ( EINKFB_SUCCESS == result )
        einkfb_debug_full("data    = 0x%04X\n", *data);
    
    return ( result );
}

int bs_rd_dat(u32 data_size, u16 *data)
{
    int result = EINKFB_FAILURE;

    // For single-word reads, don't use the buffer call.
    //
    if ( !USE_RD_BUF(data_size) )
         result = bs_rd_one(data);
    else
    {
        einkfb_debug_full("size    = %d\n", data_size);

        if ( BS_READY() )
            result = bs_io_buf(data_size, data, BS_RD);
    }
    
    return ( result );
}

/*!
 ** Interrupt function for completion of ADC buffer transfer.
 ** All we need to do is disable the channel and set the "done" flag.
 **/
static void bs_eof_irq_completion(void)
{
        ipu_disable_irq(IPU_IRQ_ADC_SYS1_EOF);
        ipu_disable_channel(ADC_SYS1, false);
        dma_xfer_done = true;
}

static irqreturn_t bs_eof_irq_handler(int irq, void *dev_id)
{
        bs_eof_irq_completion();    
        dma_stopped = jiffies;

        einkfb_debug("DMA transfer completed, time = %dms\n",
            jiffies_to_msecs(dma_stopped - dma_started));
        
        return ( IRQ_HANDLED );
}

#ifdef USE_BS_IRQ
/*
** Broadsheet IRQ handler.  Determine which condition(s) caused the
** interrupt, and take any appropriate action(s).
*/

static irqreturn_t bs_irq_handler (int irq, void *data, struct pt_regs *r)
{
    u16 irq_status;

    // Check Broadsheet general IRQs

    irq_status = bs_cmd_rd_reg(BS_INTR_RAW_STATUS_REG);
    einkfb_debug("BS_INTR_RAW_STATUS_REG = 0x%04X\n", irq_status);

    irq_status = bs_cmd_rd_reg(BS_DE_INTR_RAW_STATUS_REG);
    einkfb_debug("BS_DE_INTR_RAW_STATUS_REG = 0x%04X\n", irq_status);

    // Clear all interrupt flags
    bs_cmd_wr_reg(BS_INTR_RAW_STATUS_REG, BS_ALL_IRQS);
    bs_cmd_wr_reg(BS_DE_INTR_RAW_STATUS_REG, BS_DE_ALL_IRQS);

   return IRQ_HANDLED;
}
#endif

/*
** Initialize the Broadsheet hardware interface and reset the
** Broadsheet controller.
*/

bool bs_hw_init(void)
{
    ipu_channel_params_t params;
    int gpio_config_result;
    bool result = false;

    ipu_adc_sig_cfg_t sig = { 0, 0, 0, 0, 0, 0, 0, 0,
            IPU_ADC_BURST_WCS,
            IPU_ADC_IFC_MODE_SYS80_TYPE2,
            16, 0, 0, IPU_ADC_SER_NO_RW
    };

    // Set up ADC IRQ.
    //
    if ( ipu_request_irq(IPU_IRQ_ADC_SYS1_EOF, bs_eof_irq_handler, 0, EINKFB_NAME, NULL) )
    {
        einkfb_print_crit("Could not register SYS1 irq handler\n");
        dma_available = false;
    }
    else
    {
        bs_eof_irq_completion();
        dma_available = true;
    }
    
    memset(&params, 0, sizeof(params));
    params.adc_sys1.disp = BROADSHEET_DISPLAY_NUMBER;
    params.adc_sys1.ch_mode = WriteDataWoRS;
    ipu_init_channel(ADC_SYS1, &params);

    ipu_adc_init_panel(BROADSHEET_DISPLAY_NUMBER,
                       broadsheet_screen_width,
                       broadsheet_screen_height,
                       BROADSHEET_PIXEL_FORMAT,
                       broadsheet_screen_stride,
                       sig, XY, 0, VsyncInternal);

    // Set IPU timing for read cycles.
    //
    ipu_adc_init_ifc_timing(BROADSHEET_DISPLAY_NUMBER, true,
                            BROADSHEET_READ_CYCLE_TIME,
                            BROADSHEET_READ_UP_TIME,
                            BROADSHEET_READ_DOWN_TIME,
                            BROADSHEET_READ_LATCH_TIME,
                            BROADSHEET_PIXEL_CLK);
                            
    // Set IPU timing for write cycles.
    //
    ipu_adc_init_ifc_timing(BROADSHEET_DISPLAY_NUMBER, false,
                            BROADSHEET_WRITE_CYCLE_TIME,
                            BROADSHEET_WRITE_UP_TIME,
                            BROADSHEET_WRITE_DOWN_TIME,
                            0, 0);

    ipu_adc_set_update_mode(ADC_SYS1, IPU_ADC_REFRESH_NONE, 0, 0, 0);

#ifdef USE_BS_IRQ
    gpio_config_result = broadsheet_gpio_config((irq_handler_t)bs_irq_handler, "eink_fb_hal_broads");
#else
    gpio_config_result = broadsheet_gpio_config(NULL, NULL);
#endif
    
    switch ( gpio_config_result )
    {
        case BROADSHEET_GPIO_INIT_SUCCESS:
            // Reset Broadsheet.
            einkfb_debug("Sending RST signal to Broadsheet...\n");
            broadsheet_reset();
            einkfb_debug("Broadsheet reset done.\n");
            
#ifdef USE_BS_IRQ
            // Set up Broadsheet for interrupt generation (enable all conditions).
            bs_cmd_wr_reg(BS_INTR_CTL_REG, BS_ALL_IRQS);
            bs_cmd_wr_reg(BS_INTR_RAW_STATUS_REG, BS_ALL_IRQS);
        
            // Enable all Broadsheet display engine interrupts.
            bs_cmd_wr_reg(BS_DE_INTR_ENABLE_REG, BS_DE_ALL_IRQS);
            bs_cmd_wr_reg(BS_DE_INTR_RAW_STATUS_REG, BS_DE_ALL_IRQS);
#endif
            
            einkfb_debug("GPIOs and IRQ set; Broadsheet has been reset\n");
            result = true;
        break;
        
#ifdef USE_BS_IRQ
        case BROADSHEET_HIRQ_RQST_FAILURE:
            einkfb_print_crit("Failed IRQ request for Broadsheet HIRQ line\n");
        break;
        
        case BROADSHEET_HIRQ_INIT_FAILURE:
            einkfb_print_crit("Could not obtain GPIO pin for HIRQ\n");
        break;
#endif

        case BROADSHEET_HRST_INIT_FAILURE:
            einkfb_print_crit("Could not obtain GPIO pin for HRST\n");
        break;
        
        case BROADSHEET_HRDY_INIT_FAILURE:
            einkfb_print_crit("Could not obtain GPIO pin for HRDY\n");
        break;
    }

    return ( result );
}

void bs_hw_init_dma(void)
{
    ipu_adc_set_dma_in_use(BROADSHEET_DMA_DONE);
}

static bool bs_cmd_ck_reg(u16 ra, u16 rd)
{
    u16 rd_reg = bs_cmd_rd_reg(ra);
    bool result = true;

    if ( rd_reg != rd )
    {
        einkfb_print_crit("REG[0x%04X] is 0x%04X but should be 0x%04X\n",
            ra, rd_reg, rd);
            
        result = false;
    }
    
    return ( result );
}

bool bs_hw_test(void)
{
    bool result = true;

    bs_cmd_wr_reg(0x0304, 0x0123); // frame begin/end length reg
    bs_cmd_wr_reg(0x030A, 0x4567); // line  begin/end length reg
    result &= bs_cmd_ck_reg(0x0304, 0x0123);
    result &= bs_cmd_ck_reg(0x030A, 0x4567 );
    
    bs_cmd_wr_reg(0x0304, 0xFEDC);
    bs_cmd_wr_reg(0x030A, 0xBA98);
    result &= bs_cmd_ck_reg(0x0304, 0xFEDC);
    result &= bs_cmd_ck_reg(0x030A, 0xBA98);
    
    return ( result );
}

void bs_hw_done(void)
{
    if ( dma_available )
    {
        bs_eof_irq_completion();
        
        ipu_uninit_channel(ADC_SYS1);
        ipu_free_irq(IPU_IRQ_ADC_SYS1_EOF, NULL);
    }

#ifdef USE_BS_IRQ
    broadsheet_gpio_disable(DISABLE_BS_IRQ);
#else
    broadsheet_gpio_disable(NO_BS_IRQ);
#endif

    einkfb_debug("Released Broadsheet GPIO pins and IRQs\n");
}
