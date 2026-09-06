#include <stdint.h>
#include <odi.h>

#define FWCFG_SELECTOR 0x510
#define FWCFG_DATA     0x511
#define FWCFG_SIGNATURE 0x0000

static int probe(const odi_kernel_api *api,const odi_device *dev){
    (void)dev;
    api->pio_write16(FWCFG_SELECTOR,FWCFG_SIGNATURE);
    char sig[5];
    for(int i=0;i<4;i++)sig[i]=(char)api->pio_read8(FWCFG_DATA);
    sig[4]=0;
    return (sig[0]=='Q'&&sig[1]=='E'&&sig[2]=='M'&&sig[3]=='U')?ODI_OK:ODI_ENODEV;
}
static int attach(const odi_kernel_api *api,const odi_device *dev,void **ctx){(void)api;(void)dev;*ctx=0;return ODI_OK;}
static int start(const odi_kernel_api *api,void *ctx){(void)ctx;api->log(1,"fw_cfg: QEMU signature detected");return ODI_OK;}
static void stop(const odi_kernel_api *api,void *ctx){(void)api;(void)ctx;}
static void detach(const odi_kernel_api *api,void *ctx){(void)api;(void)ctx;}

static const odi_driver_descriptor driver={
    sizeof(odi_driver_descriptor),ODI_ABI_MAJOR,ODI_ABI_MINOR,
    ODI_CLASS_PLATFORM,0,"qemu-fw_cfg","0.1.0",probe,attach,start,stop,detach
};
const odi_driver_descriptor *odi_driver_entry(void){return &driver;}
