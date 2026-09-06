#include <odi.h>
#include <odi_services.h>
#include <stdint.h>
#include <stddef.h>

#define CR 0x00
#define PSTART 0x01
#define PSTOP 0x02
#define BNRY 0x03
#define TPSR 0x04
#define TBCR0 0x05
#define TBCR1 0x06
#define ISR 0x07
#define RSAR0 0x08
#define RSAR1 0x09
#define RBCR0 0x0a
#define RBCR1 0x0b
#define RCR 0x0c
#define TCR 0x0d
#define DCR 0x0e
#define IMR 0x0f
#define DATA 0x10
#define RESET 0x1f
#define CURR 0x07
#define PAR0 0x01
#define TX_PAGE 0x40
#define RX_START 0x46
#define RX_STOP 0x80
#define CR_STP 0x01
#define CR_STA 0x02
#define CR_TXP 0x04
#define CR_RD0 0x08
#define CR_RD1 0x10
#define CR_RD2 0x20
#define CR_PS0 0x40
#define ISR_PRX 0x01
#define ISR_PTX 0x02
#define ISR_RDC 0x40
#define ISR_RST 0x80

typedef struct {
    const odi_kernel_api *api;
    uint16_t base;
    uint8_t irq;
    uint8_t mac[6];
    odi_net_rx_handler rx;
    void *rx_context;
    uint64_t service_id;
} ne2k_ctx;

static uint8_t r8(ne2k_ctx*c,uint16_t r){return c->api->pio_read8((uint16_t)(c->base+r));}
static uint16_t r16(ne2k_ctx*c,uint16_t r){return c->api->pio_read16((uint16_t)(c->base+r));}
static void w8(ne2k_ctx*c,uint16_t r,uint8_t v){c->api->pio_write8((uint16_t)(c->base+r),v);}
static void w16(ne2k_ctx*c,uint16_t r,uint16_t v){c->api->pio_write16((uint16_t)(c->base+r),v);}
static int wait_isr(ne2k_ctx*c,uint8_t bit){for(unsigned i=0;i<200000;i++)if(r8(c,ISR)&bit)return 1;return 0;}
static void remote_setup(ne2k_ctx*c,uint16_t addr,uint16_t count,uint8_t command){
    w8(c,CR,CR_STA|CR_RD2);w8(c,ISR,ISR_RDC);w8(c,RBCR0,(uint8_t)count);w8(c,RBCR1,(uint8_t)(count>>8));w8(c,RSAR0,(uint8_t)addr);w8(c,RSAR1,(uint8_t)(addr>>8));w8(c,CR,CR_STA|command);
}
static int remote_read(ne2k_ctx*c,uint16_t addr,void*dst,uint16_t count){
    uint8_t*out=(uint8_t*)dst;
    uint16_t wire=(uint16_t)((count+1u)&~1u);
    remote_setup(c,addr,wire,CR_RD0);
    for(uint16_t i=0;i<wire;i+=2){
        uint16_t v=r16(c,DATA);
        if(i<count)out[i]=(uint8_t)v;
        if(i+1<count)out[i+1]=(uint8_t)(v>>8);
    }
    if(!wait_isr(c,ISR_RDC))return ODI_EIO;
    w8(c,ISR,ISR_RDC);
    return ODI_OK;
}
static int remote_write(ne2k_ctx*c,uint16_t addr,const void*src,uint16_t count){
    const uint8_t*in=(const uint8_t*)src;
    uint16_t wire=(uint16_t)((count+1u)&~1u);
    remote_setup(c,addr,wire,CR_RD1);
    for(uint16_t i=0;i<wire;i+=2){
        uint16_t v=i<count?in[i]:0;
        if(i+1<count)v|=(uint16_t)in[i+1]<<8;
        w16(c,DATA,v);
    }
    if(!wait_isr(c,ISR_RDC))return ODI_EIO;
    w8(c,ISR,ISR_RDC);
    return ODI_OK;
}
static int read_prom(ne2k_ctx*c,uint8_t mac[6]){
    uint8_t prom[32];
    w8(c,CR,CR_STP|CR_RD2);w8(c,DCR,0x49);w8(c,RCR,0x20);w8(c,TCR,0x02);w8(c,RBCR0,0);w8(c,RBCR1,0);
    if(remote_read(c,0,prom,sizeof(prom))!=ODI_OK)return 0;
    for(int i=0;i<6;i++)mac[i]=prom[i*2];
    int all0=1,allf=1;
    for(int i=0;i<6;i++){if(mac[i])all0=0;if(mac[i]!=0xff)allf=0;}
    return !all0&&!allf;
}
static void reset_chip(ne2k_ctx*c){uint8_t v=r8(c,RESET);w8(c,RESET,v);(void)wait_isr(c,ISR_RST);w8(c,ISR,0xff);}
static int init_chip(ne2k_ctx*c){
    reset_chip(c);
    w8(c,CR,CR_STP|CR_RD2);w8(c,DCR,0x49);w8(c,RBCR0,0);w8(c,RBCR1,0);w8(c,RCR,0x20);w8(c,TCR,0x02);w8(c,TPSR,TX_PAGE);w8(c,PSTART,RX_START);w8(c,BNRY,RX_START);w8(c,PSTOP,RX_STOP);w8(c,ISR,0xff);w8(c,IMR,0);
    w8(c,CR,CR_STP|CR_RD2|CR_PS0);for(int i=0;i<6;i++)w8(c,(uint16_t)(PAR0+i),c->mac[i]);w8(c,CURR,RX_START+1);w8(c,CR,CR_STA|CR_RD2);w8(c,TCR,0);w8(c,RCR,0x04);w8(c,IMR,ISR_PRX|ISR_PTX);
    return ODI_OK;
}
static int receive_one(ne2k_ctx*c){
    uint8_t bnry=r8(c,BNRY);
    w8(c,CR,CR_STA|CR_RD2|CR_PS0);
    uint8_t curr=r8(c,CURR);
    w8(c,CR,CR_STA|CR_RD2);
    uint8_t page=(uint8_t)(bnry+1);
    if(page>=RX_STOP)page=RX_START;
    if(page==curr)return 0;
    uint8_t hdr[4];
    if(remote_read(c,(uint16_t)page<<8,hdr,4)!=ODI_OK)return 0;
    uint8_t next=hdr[1];
    uint16_t total=(uint16_t)hdr[2]|((uint16_t)hdr[3]<<8);
    if(total<4||total>1604||next<RX_START||next>=RX_STOP){w8(c,BNRY,(uint8_t)(curr==RX_START?RX_STOP-1:curr-1));return 0;}
    uint8_t frame[1600];
    uint16_t len=(uint16_t)(total-4);
    if(remote_read(c,((uint16_t)page<<8)+4,frame,len)==ODI_OK&&c->rx)c->rx(c->rx_context,frame,len);
    w8(c,BNRY,(uint8_t)(next==RX_START?RX_STOP-1:next-1));
    return 1;
}
static int irq_handler(void*opaque){
    ne2k_ctx*c=(ne2k_ctx*)opaque;
    uint8_t isr=r8(c,ISR);
    if(!isr)return 0;
    if(isr&ISR_PRX)while(receive_one(c)){}
    w8(c,ISR,isr);
    return 1;
}
static int net_get_mac(void*opaque,uint8_t out[6]){ne2k_ctx*c=opaque;for(int i=0;i<6;i++)out[i]=c->mac[i];return ODI_OK;}
static uint32_t net_mtu(void*opaque){(void)opaque;return 1500;}
static int net_tx(void*opaque,const void*frame,size_t length){
    ne2k_ctx*c=opaque;
    if(!frame||length<14||length>1518)return ODI_EINVAL;
    uint16_t wire=(uint16_t)(length<60?60:length);
    uint8_t tmp[60];
    const void*src=frame;
    if(length<60){for(unsigned i=0;i<60;i++)tmp[i]=i<length?((const uint8_t*)frame)[i]:0;src=tmp;}
    if(remote_write(c,(uint16_t)TX_PAGE<<8,src,wire)!=ODI_OK)return ODI_EIO;
    w8(c,TPSR,TX_PAGE);w8(c,TBCR0,(uint8_t)wire);w8(c,TBCR1,(uint8_t)(wire>>8));w8(c,CR,CR_STA|CR_TXP|CR_RD2);
    return ODI_OK;
}
static int net_set_rx(void*opaque,odi_net_rx_handler h,void*hc){ne2k_ctx*c=opaque;c->rx=h;c->rx_context=hc;return ODI_OK;}
static const odi_net_service NET_OPS={{ODI_SERVICE_ABI_VERSION,sizeof(odi_net_service),0},net_get_mac,net_mtu,net_tx,net_set_rx,{0}};

static int probe(const odi_kernel_api*api,const odi_device*dev){
    if(!api||!dev||dev->bus_type!=ODI_BUS_ISA||!api->pio_read8||!api->pio_write8||!api->pio_read16||!api->pio_write16)return ODI_ENODEV;
    ne2k_ctx t={0};
    t.api=api;t.base=dev->id.isa.io_base;
    if(!t.base)return ODI_ENODEV;
    reset_chip(&t);
    uint8_t mac[6];
    return read_prom(&t,mac)?ODI_OK:ODI_ENODEV;
}
static int attach(const odi_kernel_api*api,const odi_device*dev,void**out){
    if(!api||!dev||!out||!api->alloc_pages)return ODI_EINVAL;
    void*p=0;uint64_t phys=0;
    if(api->alloc_pages(1,0xffffffffu,&p,&phys)!=ODI_OK)return ODI_ENOMEM;
    (void)phys;
    ne2k_ctx*c=(ne2k_ctx*)p;
    for(size_t i=0;i<sizeof(*c);i++)((uint8_t*)c)[i]=0;
    c->api=api;c->base=dev->id.isa.io_base;c->irq=dev->id.isa.irq;
    reset_chip(c);
    if(!read_prom(c,c->mac)){api->free_pages(p,1);return ODI_ENODEV;}
    *out=c;
    return ODI_OK;
}
static int start(const odi_kernel_api*api,void*opaque){
    ne2k_ctx*c=opaque;
    if(init_chip(c)!=ODI_OK)return ODI_EIO;
    if(c->irq&&api->irq_register&&api->irq_register(c->irq,irq_handler,c)!=ODI_OK)return ODI_EIO;
    if((api->capabilities&ODI_KERNEL_CAP_SERVICE_REGISTRY)&&api->service_publish)
        return api->service_publish(ODI_CLASS_NETWORK,&NET_OPS,sizeof(NET_OPS),c,&c->service_id);
    return ODI_OK;
}
static void stop(const odi_kernel_api*api,void*opaque){ne2k_ctx*c=opaque;if(c->service_id&&api->service_remove)api->service_remove(c->service_id);if(c->irq&&api->irq_unregister)api->irq_unregister(c->irq,irq_handler,c);w8(c,CR,CR_STP|CR_RD2);}
static void detach(const odi_kernel_api*api,void*opaque){if(api&&api->free_pages&&opaque)api->free_pages(opaque,1);}
static const odi_driver_descriptor DRIVER={
    .struct_size=sizeof(odi_driver_descriptor),.abi_major=ODI_ABI_MAJOR,.abi_minor=ODI_ABI_MINOR,.driver_class=ODI_CLASS_NETWORK,.name="odi-ne2000",.version="0.1.0",
    .probe=probe,.attach=attach,.start=start,.stop=stop,.detach=detach,
    .required_kernel_capabilities=ODI_KERNEL_CAP_PIO|ODI_KERNEL_CAP_ISA,
    .supported_architectures=ODI_ARCH_BIT(ODI_ARCH_I686)|ODI_ARCH_BIT(ODI_ARCH_X86_64)
};
const odi_driver_descriptor*odi_driver_entry(void){return &DRIVER;}
