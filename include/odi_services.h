#ifndef ORION_DRIVER_SERVICES_H
#define ORION_DRIVER_SERVICES_H
#include <stdint.h>
#include <stddef.h>
#define ODI_SERVICE_ABI_MAJOR 1u
#define ODI_SERVICE_ABI_MINOR 0u
#define ODI_SERVICE_ABI_VERSION ((ODI_SERVICE_ABI_MAJOR<<16)|ODI_SERVICE_ABI_MINOR)
typedef struct odi_service_header{uint32_t abi_version,struct_size;uint64_t capabilities;}odi_service_header;
typedef void(*odi_net_rx_handler)(void*,const void*,size_t);
typedef struct odi_net_service{odi_service_header header;int(*get_mac)(void*,uint8_t[6]);uint32_t(*get_mtu)(void*);int(*transmit)(void*,const void*,size_t);int(*set_rx_handler)(void*,odi_net_rx_handler,void*);void*reserved[4];}odi_net_service;
typedef struct odi_block_service{odi_service_header header;uint32_t(*sector_size)(void*);uint64_t(*sector_count)(void*);int(*read)(void*,uint64_t,uint32_t,void*,size_t);int(*write)(void*,uint64_t,uint32_t,const void*,size_t);int(*flush)(void*);void*reserved[4];}odi_block_service;
#define ODI_PIXEL_XRGB8888 1u
#define ODI_PIXEL_BGRX8888 2u
#define ODI_PIXEL_RGB565 3u
typedef struct odi_display_mode{uint32_t width,height,stride_pixels,pixel_format;}odi_display_mode;
typedef struct odi_display_service{odi_service_header header;int(*get_mode)(void*,odi_display_mode*);int(*set_mode)(void*,uint32_t,uint32_t,uint32_t);void*(*map_framebuffer)(void*,size_t*);void(*unmap_framebuffer)(void*,void*,size_t);int(*present)(void*,uint32_t,uint32_t,uint32_t,uint32_t);void*reserved[4];}odi_display_service;
typedef enum{ODI_INPUT_KEY=1,ODI_INPUT_POINTER=2,ODI_INPUT_WHEEL=3,ODI_INPUT_TOUCH=4}odi_input_event_type;
typedef struct odi_input_event{uint32_t type,code;int32_t value0,value1,value2;uint64_t timestamp_ns;}odi_input_event;
typedef void(*odi_input_handler)(void*,const odi_input_event*);
typedef struct odi_input_service{odi_service_header header;int(*set_handler)(void*,odi_input_handler,void*);void*reserved[6];}odi_input_service;
typedef struct odi_audio_format{uint32_t sample_rate;uint16_t channels,bits_per_sample;}odi_audio_format;
typedef struct odi_audio_service{odi_service_header header;int(*set_format)(void*,const odi_audio_format*);int(*write_pcm)(void*,const void*,size_t);uint32_t(*buffered_frames)(void*);void*reserved[5];}odi_audio_service;
#endif
