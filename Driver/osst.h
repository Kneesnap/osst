/*
 *	$Header$
 */

/*	FIXME - rename and use the following two types or delete them!
 *              and the types really should go to st.h anyway...
 *	INQUIRY packet command - Data Format (From Table 6-8 of QIC-157C)
 */
typedef struct {
	unsigned	device_type	:5;	/* Peripheral Device Type */
	unsigned	reserved0_765	:3;	/* Peripheral Qualifier - Reserved */
	unsigned	reserved1_6t0	:7;	/* Reserved */
	unsigned	rmb		:1;	/* Removable Medium Bit */
	unsigned	ansi_version	:3;	/* ANSI Version */
	unsigned	ecma_version	:3;	/* ECMA Version */
	unsigned	iso_version	:2;	/* ISO Version */
	unsigned	response_format :4;	/* Response Data Format */
	unsigned	reserved3_45	:2;	/* Reserved */
	unsigned	reserved3_6	:1;	/* TrmIOP - Reserved */
	unsigned	reserved3_7	:1;	/* AENC - Reserved */
	u8		additional_length;	/* Additional Length (total_length-4) */
	u8		rsv5, rsv6, rsv7;	/* Reserved */
	u8		vendor_id[8];		/* Vendor Identification */
	u8		product_id[16];		/* Product Identification */
	u8		revision_level[4];	/* Revision Level */
	u8		vendor_specific[20];	/* Vendor Specific - Optional */
	u8		reserved56t95[40];	/* Reserved - Optional */
						/* Additional information may be returned */
} idetape_inquiry_result_t;

/*
 *	READ POSITION packet command - Data Format (From Table 6-57)
 */
typedef struct {
	unsigned	reserved0_10	:2;	/* Reserved */
	unsigned	bpu		:1;	/* Block Position Unknown */	
	unsigned	reserved0_543	:3;	/* Reserved */
	unsigned	eop		:1;	/* End Of Partition */
	unsigned	bop		:1;	/* Beginning Of Partition */
	u8		partition;		/* Partition Number */
	u8		reserved2, reserved3;	/* Reserved */
	u32		first_block;		/* First Block Location */
	u32		last_block;		/* Last Block Location (Optional) */
	u8		reserved12;		/* Reserved */
	u8		blocks_in_buffer[3];	/* Blocks In Buffer - (Optional) */
	u32		bytes_in_buffer;	/* Bytes In Buffer (Optional) */
} idetape_read_position_result_t;

/*
 *      Follows structures which are related to the SELECT SENSE / MODE SENSE
 *      packet commands. 
 */
#define COMPRESSION_PAGE           0x0f
#define COMPRESSION_PAGE_LENGTH    16

#define CAPABILITIES_PAGE          0x2a
#define CAPABILITIES_PAGE_LENGTH   20

#define TAPE_PARAMTR_PAGE          0x2b
#define TAPE_PARAMTR_PAGE_LENGTH   16

#define NUMBER_RETRIES_PAGE        0x2f
#define NUMBER_RETRIES_PAGE_LENGTH 4

#define BLOCK_SIZE_PAGE            0x30
#define BLOCK_SIZE_PAGE_LENGTH     4

#define BUFFER_FILLING_PAGE        0x33
#define BUFFER_FILLING_PAGE_LENGTH 

#define VENDOR_IDENT_PAGE          0x36
#define VENDOR_IDENT_PAGE_LENGTH   8

#define LOCATE_STATUS_PAGE         0x37
#define LOCATE_STATUS_PAGE_LENGTH  0

#define MODE_HEADER_LENGTH         4


/*
 *	REQUEST SENSE packet command result - Data Format.
 */
typedef struct {
	unsigned	error_code	:7;	/* Current of deferred errors */
	unsigned	valid		:1;	/* The information field conforms to QIC-157C */
	u8		reserved1	:8;	/* Segment Number - Reserved */
	unsigned	sense_key	:4;	/* Sense Key */
	unsigned	reserved2_4	:1;	/* Reserved */
	unsigned	ili		:1;	/* Incorrect Length Indicator */
	unsigned	eom		:1;	/* End Of Medium */
	unsigned	filemark 	:1;	/* Filemark */
	u32		information __attribute__ ((packed));
	u8		asl;			/* Additional sense length (n-7) */
	u32		command_specific;	/* Additional command specific information */
	u8		asc;			/* Additional Sense Code */
	u8		ascq;			/* Additional Sense Code Qualifier */
	u8		replaceable_unit_code;	/* Field Replaceable Unit Code */
	unsigned	sk_specific1 	:7;	/* Sense Key Specific */
	unsigned	sksv		:1;	/* Sense Key Specific information is valid */
	u8		sk_specific2;		/* Sense Key Specific */
	u8		sk_specific3;		/* Sense Key Specific */
	u8		pad[2];			/* Padding to 20 bytes */
} idetape_request_sense_result_t;

/*
 *      Mode Parameter Header for the MODE SENSE packet command
 */
typedef struct {
        u8              mode_data_length;       /* Length of the following data transfer */
        u8              medium_type;            /* Medium Type */
        u8              dsp;                    /* Device Specific Parameter */
        u8              bdl;                    /* Block Descriptor Length */
} osst_mode_parameter_header_t;

/*
 *      Mode Parameter Block Descriptor the MODE SENSE packet command
 *
 *      Support for block descriptors is optional.
 */
typedef struct {
        u8              density_code;           /* Medium density code */
        u8              blocks[3];              /* Number of blocks */
        u8              reserved4;              /* Reserved */
        u8              length[3];              /* Block Length */
} osst_parameter_block_descriptor_t;

/*
 *      The Data Compression Page, as returned by the MODE SENSE packet command.
 */
typedef struct {
        unsigned        page_code       :6;     /* Page Code - Should be 0xf */
        unsigned        reserved0       :1;     /* Reserved */
        unsigned        ps              :1;
        u8              page_length;            /* Page Length - Should be 14 */
        unsigned        reserved2       :6;     /* Reserved */
        unsigned        dcc             :1;     /* Data Compression Capable */
        unsigned        dce             :1;     /* Data Compression Enable */
        unsigned        reserved3       :5;     /* Reserved */
        unsigned        red             :2;     /* Report Exception on Decompression */
        unsigned        dde             :1;     /* Data Decompression Enable */
        u32             ca;                     /* Compression Algorithm */
        u32             da;                     /* Decompression Algorithm */
        u8              reserved[4];            /* Reserved */
} osst_data_compression_page_t;

/*
 *      The Medium Partition Page, as returned by the MODE SENSE packet command.
 */
typedef struct {
        unsigned        page_code       :6;     /* Page Code - Should be 0x11 */
        unsigned        reserved1_6     :1;     /* Reserved */
        unsigned        ps              :1;
        u8              page_length;            /* Page Length - Should be 6 */
        u8              map;                    /* Maximum Additional Partitions - Should be 0 */
        u8              apd;                    /* Additional Partitions Defined - Should be 0 */
        unsigned        reserved4_012   :3;     /* Reserved */
        unsigned        psum            :2;     /* Should be 0 */
        unsigned        idp             :1;     /* Should be 0 */
        unsigned        sdp             :1;     /* Should be 0 */
        unsigned        fdp             :1;     /* Fixed Data Partitions */
        u8              mfr;                    /* Medium Format Recognition */
        u8              reserved[2];            /* Reserved */
} osst_medium_partition_page_t;

/*
 *      Capabilities and Mechanical Status Page
 */
typedef struct {
        unsigned        page_code       :6;     /* Page code - Should be 0x2a */
        unsigned        reserved1_67    :2;
        u8              page_length;            /* Page Length - Should be 0x12 */
        u8              reserved2, reserved3;
        unsigned        ro              :1;     /* Read Only Mode */
        unsigned        reserved4_1234  :4;
        unsigned        sprev           :1;     /* Supports SPACE in the reverse direction */
        unsigned        reserved4_67    :2;
        unsigned        reserved5_012   :3;
        unsigned        efmt            :1;     /* Supports ERASE command initiated formatting */
        unsigned        reserved5_4     :1;
        unsigned        qfa             :1;     /* Supports the QFA two partition formats */
        unsigned        reserved5_67    :2;
        unsigned        lock            :1;     /* Supports locking the volume */
        unsigned        locked          :1;     /* The volume is locked */
        unsigned        prevent         :1;     /* The device defaults in the prevent state after power up */
        unsigned        eject           :1;     /* The device can eject the volume */
	unsigned        reserved6_45    :2;     /* Reserved */  
        unsigned        ecc             :1;     /* Supports error correction */
        unsigned        cmprs           :1;     /* Supports data compression */
        unsigned        reserved7_0     :1;
        unsigned        blk512          :1;     /* Supports 512 bytes block size */
        unsigned        blk1024         :1;     /* Supports 1024 bytes block size */
        unsigned        reserved7_3_6   :4;
        unsigned        blk32768        :1;     /* slowb - the device restricts the byte count for PIO */
                                                /* transfers for slow buffer memory ??? */
                                                /* Also 32768 block size in some cases */
        u16             max_speed;              /* Maximum speed supported in KBps */
        u8              reserved10, reserved11;
        u16             ctl;                    /* Continuous Transfer Limit in blocks */
        u16             speed;                  /* Current Speed, in KBps */
        u16             buffer_size;            /* Buffer Size, in 512 bytes */
        u8              reserved18, reserved19;
} osst_capabilities_page_t;

/*
 *      Block Size Page
 */
typedef struct {
        unsigned        page_code       :6;     /* Page code - Should be 0x30 */
        unsigned        reserved1_6     :1;
        unsigned        ps              :1;
        u8              page_length;            /* Page Length - Should be 2 */
        u8              reserved2;
        unsigned        play32          :1;
        unsigned        play32_5        :1;
        unsigned        reserved2_23    :2;
        unsigned        record32        :1;
        unsigned        record32_5      :1;
        unsigned        reserved2_6     :1;
        unsigned        one             :1;
} osst_block_size_page_t;

/*
 *	Tape Parameters Page
 */
typedef struct {
        unsigned        page_code       :6;     /* Page code - Should be 0x2b */
        unsigned        reserved1_6     :1;
        unsigned        ps              :1;
	u8		reserved2;
	u8		density;
	u8		reserved3,reserved4;
	u16		segtrk;
	u16		trks;
	u8		reserved5,reserved6,reserved7,reserved8,reserved9,reserved10;
} osst_tape_paramtr_page_t;

/* OnStream definitions */

#define OS_CONFIG_PARTITION     (0xff)
#define OS_DATA_PARTITION       (0)
#define OS_PARTITION_VERSION    (1)

/*
 * partition
 */
typedef struct os_partition_s {
        __u8    partition_num;
        __u8    par_desc_ver;
        __u16   wrt_pass_cntr;
        __u32   first_frame_addr;
        __u32   last_frame_addr;
        __u32   eod_frame_addr;
} os_partition_t;

/*
 * DAT entry
 */
typedef struct os_dat_entry_s {
        __u32   blk_sz;
        __u16   blk_cnt;
        __u8    flags;
        __u8    reserved;
} os_dat_entry_t;

/*
 * DAT
 */
#define OS_DAT_FLAGS_DATA       (0xc)
#define OS_DAT_FLAGS_MARK       (0x1)

typedef struct os_dat_s {
        __u8            dat_sz;
        __u8            reserved1;
        __u8            entry_cnt;
        __u8            reserved3;
        os_dat_entry_t  dat_list[16];
} os_dat_t;

/*
 * Frame types
 */
#define OS_FRAME_TYPE_FILL      (0)
#define OS_FRAME_TYPE_EOD       (1 << 0)
#define OS_FRAME_TYPE_MARKER    (1 << 1)
#define OS_FRAME_TYPE_HEADER    (1 << 3)
#define OS_FRAME_TYPE_DATA      (1 << 7)

/*
 * AUX
 */
typedef struct os_aux_s {
        __u32           format_id;              /* hardware compability AUX is based on */
        char            application_sig[4];     /* driver used to write this media */
        __u32           hdwr;                   /* reserved */
        __u32           update_frame_cntr;      /* for configuration frame */
        __u8            frame_type;
        __u8            frame_type_reserved;
        __u8            reserved_18_19[2];
        os_partition_t  partition;
        __u8            reserved_36_43[8];
        __u32           frame_seq_num;
        __u32           logical_blk_num_high;
        __u32           logical_blk_num;
        os_dat_t        dat;
        __u8            reserved188_191[4];
        __u32           filemark_cnt;
        __u32           phys_fm;
        __u32           last_mark_addr;
        __u8            reserved204_223[20];

        /*
         * __u8         app_specific[32];
         *
         * Linux specific fields:
         */
         __u32          next_mark_addr;         /* when known, points to next marker */
         __u8           linux_specific[28];

        __u8            reserved_256_511[256];
} os_aux_t;

typedef struct os_header_s {
        char            ident_str[8];
        __u8            major_rev;
        __u8            minor_rev;
        __u8            reserved10_15[6];
        __u8            par_num;
        __u8            reserved1_3[3];
        os_partition_t  partition;
} os_header_t;


/*
 * OnStream ADRL frame
 */
#define OS_FRAME_SIZE   (32 * 1024 + 512)
#define OS_DATA_SIZE    (32 * 1024)
#define OS_AUX_SIZE     (512)

