#include <odi.h>
#include <odi_services.h>
#include <stdint.h>
#include <stddef.h>

#define ATA_DATA 0
#define ATA_ERROR 1
#define ATA_SECCOUNT 2
#define ATA_LBA0 3
#define ATA_LBA1 4
#define ATA_LBA2 5
#define ATA_DRIVE 6
#define ATA_STATUS 7
#define ATA_COMMAND 7
#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF 0x20
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01
#define ATA_CMD_READ 0x20
#define ATA_CMD_WRITE 0x30
#define ATA_CMD_FLUSH 0xe7
#define ATA_CMD_IDENTIFY 0xec

typedef struct {
    const odi_kernel_api *api;
    uint16_t base;
    uint8_t irq;
    uint64_t sectors;
    uint64_t service_id;
} ata_ctx;

static uint8_t r8(ata_ctx*c,uint16_t r){return c->api->pio_read8((uint16_t)(c->base+r));}
static uint16_t r16(ata_ctx*c,uint16_t r){return c->api->pio_read16((uint16_t)(c->base+r));}
static void w8(ata_ctx*c,uint16_t r,uint8_t v){c->api->pio_write8((uint16_t)(c->base+r),v);}
static void w16(ata_ctx*c,uint16_t r,uint16_t v){c->api->pio_write16((uint16_t)(c->base+r),v);}
static void io_delay(ata_ctx*c){(void)r8(c,ATA_STATUS);(void)r8(c,ATA_STATUS);(void)r8(c,ATA_STATUS);(void)r8(c,ATA_STATUS);}
static int wait_not_busy(ata_ctx*c){for(unsigned n=0;n<1000000;n++){uint8_t s=r8(c,ATA_STATUS);if(!(s&ATA_SR_BSY))return (s&(ATA_SR_ERR|ATA_SR_DF))?ODI_EIO:ODI_OK;}return ODI_EIO;}
static int wait_drq(ata_ctx*c){for(unsigned n=0;n<1000000;n++){uint8_t s=r8(c,ATA_STATUS);if(s&(ATA_SR_ERR|ATA_SR_DF))return ODI_EIO;if(!(s&ATA_SR_BSY)&&(s&ATA_SR_DRQ))return ODI_OK;}return ODI_EIO;}
static int identify(ata_ctx*c,uint16_t out[256]){
    w8(c,ATA_DRIVE,0xa0);io_delay(c);w8(c,ATA_SECCOUNT,0);w8(c,ATA_LBA0,0);w8(c,ATA_LBA1,0);w8(c,ATA_LBA2,0);w8(c,ATA_COMMAND,ATA_CMD_IDENTIFY);
    uint8_t s=r8(c,ATA_STATUS);
    if(s==0||s==0xff)return ODI_ENODEV;
    if(wait_not_busy(c)!=ODI_OK)return ODI_EIO;
    if(r8(c,ATA_LBA1)||r8(c,ATA_LBA2))return ODI_ENODEV;
    if(wait_drq(c)!=ODI_OK)return ODI_EIO;
    for(int i=0;i<256;i++)out[i]=r16(c,ATA_DATA);
    return ODI_OK;
}
static int select_lba28(ata_ctx*c,uint32_t lba,uint8_t count,uint8_t command){
    if(lba>=0x10000000u||!count)return ODI_EINVAL;
    if(wait_not_busy(c)!=ODI_OK)return ODI_EIO;
    w8(c,ATA_DRIVE,(uint8_t)(0xe0|((lba>>24)&0x0f)));io_delay(c);w8(c,ATA_SECCOUNT,count);w8(c,ATA_LBA0,(uint8_t)lba);w8(c,ATA_LBA1,(uint8_t)(lba>>8));w8(c,ATA_LBA2,(uint8_t)(lba>>16));w8(c,ATA_COMMAND,command);return ODI_OK;
}
static uint32_t block_sector_size(void*opaque){(void)opaque;return 512;}
static uint64_t block_sector_count(void*opaque){return ((ata_ctx*)opaque)->sectors;}
static int block_read(void*opaque,uint64_t lba64,uint32_t sectors,void*buffer,size_t bytes){
    ata_ctx*c=opaque;if(!buffer||!sectors||sectors>255||bytes<(size_t)sectors*512||lba64+sectors>c->sectors||lba64>=0x10000000ull)return ODI_EINVAL;
    uint8_t*out=buffer;if(select_lba28(c,(uint32_t)lba64,(uint8_t)sectors,ATA_CMD_READ)!=ODI_OK)return ODI_EIO;
    for(uint32_t s=0;s<sectors;s++){if(wait_drq(c)!=ODI_OK)return ODI_EIO;for(int i=0;i<256;i++){uint16_t v=r16(c,ATA_DATA);*out++=(uint8_t)v;*out++=(uint8_t)(v>>8);}io_delay(c);}return ODI_OK;
}
static int block_write(void*opaque,uint64_t lba64,uint32_t sectors,const void*buffer,size_t bytes){
    ata_ctx*c=opaque;if(!buffer||!sectors||sectors>255||bytes<(size_t)sectors*512||lba64+sectors>c->sectors||lba64>=0x10000000ull)return ODI_EINVAL;
    const uint8_t*in=buffer;if(select_lba28(c,(uint32_t)lba64,(uint8_t)sectors,ATA_CMD_WRITE)!=ODI_OK)return ODI_EIO;
    for(uint32_t s=0;s<sectors;s++){if(wait_drq(c)!=ODI_OK)return ODI_EIO;for(int i=0;i<256;i++){uint16_t v=(uint16_t)in[0]|((uint16_t)in[1]<<8);in+=2;w16(c,ATA_DATA,v);}io_delay(c);}return wait_not_busy(c);
}
static int block_flush(void*opaque){ata_ctx*c=opaque;if(wait_not_busy(c)!=ODI_OK)return ODI_EIO;w8(c,ATA_COMMAND,ATA_CMD_FLUSH);return wait_not_busy(c);}
static const odi_block_service BLOCK_OPS={{ODI_SERVICE_ABI_VERSION,sizeof(odi_block_service),0},block_sector_size,block_sector_count,block_read,block_write,block_flush,{0}};

static int probe(const odi_kernel_api*api,const odi_device*dev){
    if(!api||!dev||dev->bus_type!=ODI_BUS_ISA||!dev->id.isa.io_base||!api->pio_read8||!api->pio_read16||!api->pio_write8||!api->pio_write16)return ODI_ENODEV;
    ata_ctx c={api,dev->id.isa.io_base,dev->id.isa.irq,0,0};uint16_t id[256];return identify(&c,id);
}
static int attach(const odi_kernel_api*api,const odi_device*dev,void**out){
    if(!api||!dev||!out||!api->alloc_pages)return ODI_EINVAL;
    void*p=0;uint64_t phys=0;
    if(api->alloc_pages(1,0xffffffffu,&p,&phys)!=ODI_OK)return ODI_ENOMEM;
    (void)phys;
    ata_ctx*c=p;
    for(size_t i=0;i<sizeof(*c);i++)((uint8_t*)c)[i]=0;
    c->api=api;c->base=dev->id.isa.io_base;c->irq=dev->id.isa.irq;
    uint16_t id[256];int rc=identify(c,id);
    if(rc!=ODI_OK){api->free_pages(p,1);return rc;}
    c->sectors=(uint32_t)id[60]|((uint32_t)id[61]<<16);
    if(!c->sectors){api->free_pages(p,1);return ODI_ENODEV;}
    *out=c;
    return ODI_OK;
}
static int start(const odi_kernel_api*api,void*opaque){ata_ctx*c=opaque;if((api->capabilities&ODI_KERNEL_CAP_SERVICE_REGISTRY)&&api->service_publish)return api->service_publish(ODI_CLASS_BLOCK,&BLOCK_OPS,sizeof(BLOCK_OPS),c,&c->service_id);return ODI_OK;}
static void stop(const odi_kernel_api*api,void*opaque){ata_ctx*c=opaque;if(c->service_id&&api->service_remove)api->service_remove(c->service_id);}
static void detach(const odi_kernel_api*api,void*opaque){if(api&&api->free_pages&&opaque)api->free_pages(opaque,1);}
static const odi_driver_descriptor DRIVER={
    .struct_size=sizeof(odi_driver_descriptor),.abi_major=ODI_ABI_MAJOR,.abi_minor=ODI_ABI_MINOR,.driver_class=ODI_CLASS_BLOCK,.name="odi-ata-pio",.version="0.1.0",
    .probe=probe,.attach=attach,.start=start,.stop=stop,.detach=detach,
    .required_kernel_capabilities=ODI_KERNEL_CAP_PIO|ODI_KERNEL_CAP_ISA,
    .supported_architectures=ODI_ARCH_BIT(ODI_ARCH_I686)|ODI_ARCH_BIT(ODI_ARCH_X86_64)
};
const odi_driver_descriptor*odi_driver_entry(void){return &DRIVER;}
