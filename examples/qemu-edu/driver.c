#include <stdint.h>
#include <odi.h>

#define EDU_VENDOR 0x1234
#define EDU_DEVICE 0x11e8
#define EDU_REG_ID 0x00
#define EDU_REG_LIVE 0x04

static volatile uint32_t *bar0;

static int probe(const odi_kernel_api *api,const odi_device *dev){
    (void)api;
    if(!dev||dev->bus_type!=ODI_BUS_PCI)return ODI_ENODEV;
    return (dev->id.pci.vendor_id==EDU_VENDOR&&dev->id.pci.device_id==EDU_DEVICE)?ODI_OK:ODI_ENODEV;
}
static int attach(const odi_kernel_api *api,const odi_device *dev,void **ctx){
    if(!dev->id.pci.bars[0].size)return ODI_EINVAL;
    bar0=(volatile uint32_t*)api->map_mmio(dev->id.pci.bars[0].base,dev->id.pci.bars[0].size,0);
    if(!bar0)return ODI_ENOMEM;
    *ctx=(void*)bar0;
    return ODI_OK;
}
static int start(const odi_kernel_api *api,void *ctx){
    volatile uint32_t *mmio=(volatile uint32_t*)ctx;
    uint32_t id=mmio[EDU_REG_ID/4];
    (void)id;
    uint32_t test=0x13579BDFu;
    mmio[EDU_REG_LIVE/4]=test;
    if(mmio[EDU_REG_LIVE/4]!=(~test))return ODI_EIO;
    api->log(1,"qemu-edu: MMIO liveness test passed");
    return ODI_OK;
}
static void stop(const odi_kernel_api *api,void *ctx){(void)api;(void)ctx;}
static void detach(const odi_kernel_api *api,void *ctx){if(ctx)api->unmap_mmio(ctx,0x100000);bar0=0;}

static const odi_driver_descriptor driver={
    sizeof(odi_driver_descriptor),ODI_ABI_MAJOR,ODI_ABI_MINOR,
    ODI_CLASS_PLATFORM,0,"qemu-edu","0.1.0",probe,attach,start,stop,detach
};
const odi_driver_descriptor *odi_driver_entry(void){return &driver;}
