#include "programflash.h"
#include "ff.h"
#include <string.h>
#include <stdlib.h>
#include "MSXTerminal.h"
#include "usb_host_config.h"
#include "ch32v30x_flash.h"

#define CR_PG_Set ((uint32_t)0x00000001)
#define CR_PG_Reset ((uint32_t)0xFFFFFFFE)
#define CR_PER_Set ((uint32_t)0x00000002)
#define CR_PER_Reset ((uint32_t)0xFFFFFFFD)
#define CR_MER_Set ((uint32_t)0x00000004)
#define CR_MER_Reset ((uint32_t)0xFFFFFFFB)
#define CR_OPTPG_Set ((uint32_t)0x00000010)
#define CR_OPTPG_Reset ((uint32_t)0xFFFFFFEF)
#define CR_OPTER_Set ((uint32_t)0x00000020)
#define CR_OPTER_Reset ((uint32_t)0xFFFFFFDF)
#define CR_STRT_Set ((uint32_t)0x00000040)
#define CR_LOCK_Set ((uint32_t)0x00000080)
#define CR_FLOCK_Set ((uint32_t)0x00008000)
#define CR_PAGE_PG ((uint32_t)0x00010000)
#define CR_PAGE_ER ((uint32_t)0x00020000)
#define CR_BER32 ((uint32_t)0x00040000)
#define CR_BER64 ((uint32_t)0x00080000)
#define CR_PG_STRT ((uint32_t)0x00200000)
#define SR_BSY ((uint32_t)0x00000001)

extern CircularBuffer scb;

/* Flash Operation Key */
volatile uint32_t Flash_Operation_Key0;
volatile uint32_t Flash_Operation_Key1;

volatile uint32_t IAP_Load_Addr_Offset;
volatile uint32_t IAP_WriteIn_Length;
volatile uint32_t IAP_WriteIn_Count;
volatile uint32_t File_Length;

volatile uint32_t start_address = 0x08008000;
volatile uint32_t end_address = 0x08048000;

/* Variable */
__attribute__ ((aligned (4))) uint8_t IAPLoadBuffer[DEF_MAX_IAP_BUFFER_LEN];

void ProgramCart (CartType cartType, char *Filename, char *Folder) {
    // Save current RCC_CFGR settings
    uint32_t old_cfgr = RCC->CFGR0;

    uint32_t totalcount, t;
    uint16_t ret;

    FIL file;
    FRESULT fres;
    UINT bytesRead;
    DWORD fileSize;
    // Mount the filesystem (assume drive number 0)
    FATFS fs;
    do {
        fres = f_mount (&fs, "", 1);
        if (fres != FR_OK) {
            Delay_Ms (100);
        }
    } while (fres != FR_OK);

    char fullPath[256];
    CombinePath (fullPath, Folder, Filename);
    // Open the file
    fres = f_open (&file, fullPath, FA_READ);
    if (fres != FR_OK) {
        appendString (&scb, "File Not Found.");
        NewLine();
        return;
    }

    // Get file size
    fileSize = f_size (&file);
    if (fileSize > 262144)  // 256K limit check
    {
        NewLine();
        appendString (&scb, "256KB limit exceeded!");
        NewLine();
        Delay_Ms (2000);
        PrintMainMenu (0);
        f_close (&file);
        return;
    }

    // Erase flash region before programming
    Flash_Operation_Key0 = DEF_FLASH_OPERATION_KEY_CODE_0;
    Flash_Operation_Key1 = DEF_FLASH_OPERATION_KEY_CODE_1;
    NewLine();
    appendString (&scb, " Erasing flash...");
    NewLine();
    waitBufferEmpty (&scb);


    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_HPRE) | RCC_HPRE_DIV2;
    Delay_Us (20);
    FLASH_ROM_ERASE (start_address, fileSize);

    // Restore original RCC_CFGR settings (including HCLK divider)
    RCC->CFGR0 = old_cfgr;


    Flash_Operation_Key1 = 0;
    totalcount = fileSize;
    File_Length = totalcount;
    NewLine();
    appendString (&scb, " Programming and verifying...");
    NewLine();
    append (&scb, 0x20);
    MoveCursor (9, 1);
    appendString (&scb, "Progress: 0%");
    waitBufferEmpty (&scb);
    Flash_Operation_Key0 = DEF_FLASH_OPERATION_KEY_CODE_0;
    IAP_Load_Addr_Offset = 0;
    IAP_WriteIn_Length = 0;
    IAP_WriteIn_Count = 0;
    while (totalcount) {
        // Read in chunks, up to DEF_MAX_IAP_BUFFER_LEN or less if at end
        if (totalcount > DEF_MAX_IAP_BUFFER_LEN) {
            t = DEF_MAX_IAP_BUFFER_LEN;
        } else {
            t = totalcount;
        }
        fres = f_read (&file, IAPLoadBuffer, t, &bytesRead);
        if (fres != FR_OK) {
            appendString (&scb, "Read error.");
            f_close (&file);
            return;
        }
        totalcount -= bytesRead;

        RCC->CFGR0 = (RCC->CFGR0 & ~RCC_HPRE) | RCC_HPRE_DIV2;
        Delay_Us (20);
        ret = IAP_Flash_Program (start_address + IAP_Load_Addr_Offset, IAPLoadBuffer, bytesRead);
        // Restore original RCC_CFGR settings (including HCLK divider)
        RCC->CFGR0 = old_cfgr;
        Delay_Us (20);


        mStopIfError (ret);
        IAP_Load_Addr_Offset += bytesRead;
        IAP_WriteIn_Count += bytesRead;


        // Progress feedback every 5%
        static uint8_t last_percent = 0;
        uint8_t percent = (uint8_t)(((uint64_t)IAP_WriteIn_Count * 100) / fileSize);
        if (percent > 100)
            percent = 100;
        if (percent >= last_percent + 5) {
            last_percent = percent - (percent % 5);  // Snap to lower multiple of 5
            MoveCursor (9, 1);
            appendString (&scb, "            ");
            MoveCursor (9, 1);
            char msg[20];
            intToString (last_percent, msg);
            appendString (&scb, "Progress: ");
            appendString (&scb, msg);
            appendString (&scb, "%");
            NewLine();
            waitBufferEmpty (&scb);
        }
    }
    f_close (&file);
    // Check actual write length and file length
    if (fileSize == IAP_WriteIn_Count) {
        MapperCode_Write (cartType, File_Length, (char *)Filename);

        FLASH_Lock_Fast();
        NewLine();
        appendString (&scb, " Done. Rebooting...");
        NewLine();
        NewLine();
        appendString (&scb, " DON'T HOLD ANY KEYS!");


        Delay_Ms (1000);
        Reset();
    } else {
        appendString (&scb, " Check sum error, programming failed");
        FLASH_Lock_Fast();
    }
}

/*********************************************************************
 * @fn      FLASH_ReadByte
 *
 * @brief   Read Flash In byte(8 bits)
 *
 * @return  8bits data readout
 */
uint8_t FLASH_ReadByte (uint32_t address) {
    return *(__IO uint8_t *)address;
}

/*********************************************************************
 * @fn      FLASH_ReadHalfWord
 *
 * @brief   Read Flash In HalfWord(16 bits)
 *
 * @return  16bits data readout
 */
uint16_t FLASH_ReadHalfWord (uint32_t address) {
    return *(__IO uint16_t *)address;
}

/*********************************************************************
 * @fn      FLASH_ReadWord
 *
 * @brief   Read Flash In Word(32 bits).
 *
 * @return  32bits data readout
 */
uint32_t FLASH_ReadWord (uint32_t address) {
    return *(__IO uint32_t *)address;
}

/*********************************************************************
 * @fn      FLASH_ReadWordAdd
 *
 * @brief   Read Flash In Word(32 bits),Specify length & address
 *
 * @return  none
 */
void FLASH_ReadWordAdd (uint32_t address, u32 *buff, uint16_t length) {
    uint16_t i;

    for (i = 0; i < length; i++) {
        buff[i] = *(__IO uint32_t *)address;
        address += 4;
    }
}

/*********************************************************************
 * @fn      IAP_Flash_Read
 *
 * @brief   Read Flash In bytes(8 bits),Specify length & address,
 *          With address protection and program runaway protection.
 *
 * @return  0: Operation Success
 *          See notes for other errors
 */
uint8_t IAP_Flash_Read (uint32_t address, uint8_t *buff, uint32_t length) {
    uint32_t i;
    uint32_t read_addr;
    uint32_t read_len;
    read_addr = address;
    read_len = length;


    /* Verify Address, No flash operation if the address is out of range */
    if (((read_addr >= start_address) && (read_addr <= end_address))) {
        /* Available Data */
        for (i = 0; i < read_len; i++) {
            buff[i] = *(__IO uint8_t *)read_addr;  // Read one word of data at the specified address
            read_addr++;
        }

        if (i != read_len) {
            /* Incorrect read length */
            return 0xFD;
        }
    } else {
        /* address Out Of Range */
        return 0xFE;
    }

    return 0;
}

/*********************************************************************
 * @fn      mFLASH_ProgramPage_Fast
 *
 * @brief   Fast programming, input variable addresses and data buffers,
 *          no longer than one page at a time.
 *          The content of the functions can be written according to the
 *          chip flash fast programming process by yourself
 *
 * @return  0: Operation Success
 *          See notes for other errors
 */
uint8_t mFLASH_ProgramPage_Fast (uint32_t addr, uint32_t *buffer) {
    FLASH_ProgramPage_Fast (addr, buffer);
    return 0;
}

/*********************************************************************
 * @fn      IAP_Flash_Write
 *
 * @brief   Write Flash In bytes(8 bits),Specify length & address,
 *          Based On Fast Flash Operation,
 *          With address protection and program runaway protection.
 *
 * @return  0: Operation Success
 *          See notes for other errors
 */
uint8_t IAP_Flash_Write (uint32_t address, uint8_t *buff, uint32_t length) {
    uint32_t i, j;
    uint32_t write_addr;
    uint32_t write_len;
    uint16_t write_len_once;
    uint32_t write_cnts;
    volatile uint32_t page_cnts;
    volatile uint32_t page_addr;
    uint8_t temp_buf[DEF_FLASH_PAGE_SIZE];

    /* Set initial value */
    write_addr = address;
    page_addr = address;
    write_len = length;
    if ((write_len % DEF_FLASH_PAGE_SIZE) == 0) {
        page_cnts = write_len / DEF_FLASH_PAGE_SIZE;
    } else {
        page_cnts = (write_len / DEF_FLASH_PAGE_SIZE) + 1;
    }
    write_cnts = 0;

    /* Verify Keys, No flash operation if keys are not correct */
    if ((Flash_Operation_Key0 != DEF_FLASH_OPERATION_KEY_CODE_0) || (Flash_Operation_Key1 != DEF_FLASH_OPERATION_KEY_CODE_1)) {
        /* ERR: Risk of code running away */
        return 0xFF;
    }

    /* Verify Address, No flash operation if the address is out of range */
    if (((write_addr >= start_address) && (write_addr <= end_address))) {
        for (i = 0; i < page_cnts; i++) {
            /* Verify Keys, No flash operation if keys are not correct */
            if (Flash_Operation_Key0 != DEF_FLASH_OPERATION_KEY_CODE_0) {
                /* ERR: Risk of code running away */
                return 0xFF;
            }
            /* Determine if the length of the written packet is less than the page size */
            /* If it is less than, the original content of the memory needs to be read out first and modified before the operation can be performed */
            write_len_once = DEF_FLASH_PAGE_SIZE;
            FLASH_Unlock_Fast();
            // FLASH_ErasePage_Fast (page_addr);
            if (write_len < DEF_FLASH_PAGE_SIZE) {
                FLASH_Unlock_Fast();
                IAP_Flash_Read (page_addr, temp_buf, DEF_FLASH_PAGE_SIZE);

                for (j = 0; j < write_len; j++) {
                    temp_buf[j] = buff[DEF_FLASH_PAGE_SIZE * i + j];
                }
                write_len_once = write_len;
                mFLASH_ProgramPage_Fast (page_addr, (u32 *)&temp_buf[0]);
            } else {
                mFLASH_ProgramPage_Fast (page_addr, (u32 *)&buff[DEF_FLASH_PAGE_SIZE * i]);
            }
            page_addr += DEF_FLASH_PAGE_SIZE;
            write_len -= write_len_once;
            write_cnts++;
        }
        if (i != write_cnts) {
            /* Incorrect read length */
            return 0xFD;
        }
        FLASH_Lock_Fast();
    } else {
        /* address Out Of Range */
        return 0xFE;
    }

    return 0;
}

/*********************************************************************
 * @fn      IAP_Flash_Erase
 *
 * @brief   Erase Flash In page(256 bytes),Specify length & address,
 *          Based On Fast Flash Operation,
 *          With address protection and program runaway protection.
 *
 * @return  0: Operation Success
 *          See notes for other errors
 */
uint8_t IAP_Flash_Erase (uint32_t address, uint32_t length) {
    uint32_t i;
    uint32_t erase_addr;
    uint32_t erase_len;
    volatile uint32_t page_cnts;
    volatile uint32_t page_addr;

    /* Set initial value */
    erase_addr = address;
    page_addr = address;
    erase_len = length;
    if ((erase_len % DEF_FLASH_PAGE_SIZE) == 0) {
        page_cnts = erase_len / DEF_FLASH_PAGE_SIZE;
    } else {
        page_cnts = (erase_len / DEF_FLASH_PAGE_SIZE) + 1;
    }

    /* Verify Keys, No flash operation if keys are not correct */
    if ((Flash_Operation_Key0 != DEF_FLASH_OPERATION_KEY_CODE_0) || (Flash_Operation_Key1 != DEF_FLASH_OPERATION_KEY_CODE_1)) {
        /* ERR: Risk of code running away */
        return 0xFF;
    }
    /* Verify Address, No flash operation if the address is out of range */
    if (((erase_addr >= start_address) && (erase_addr <= end_address))) {
        for (i = 0; i < page_cnts; i++) {
            /* Verify Keys, No flash operation if keys are not correct */
            if (Flash_Operation_Key0 != DEF_FLASH_OPERATION_KEY_CODE_0) {
                /* ERR: Risk of code running away */
                return 0xFF;
            }
            FLASH_Unlock_Fast();
            FLASH_ErasePage_Fast (page_addr);
            page_addr += DEF_FLASH_PAGE_SIZE;
        }
    }

    return 0;
}

/*********************************************************************
 * @fn      IAP_Flash_Verify
 *
 * @brief   verify Flash In bytes(8 bits),Specify length & address,
 *          With address protection and program runaway protection.
 *
 * @return  0: Operation Success
 *          See notes for other errors
 */
uint32_t IAP_Flash_Verify (uint32_t address, uint8_t *buff, uint32_t length) {
    uint32_t i;
    uint32_t read_addr;
    uint32_t read_len;

    /* Set initial value */
    read_addr = address;
    read_len = length;

    /* Verify Keys, No flash operation if keys are not correct */
    if ((Flash_Operation_Key0 != DEF_FLASH_OPERATION_KEY_CODE_0) || (Flash_Operation_Key1 != DEF_FLASH_OPERATION_KEY_CODE_1)) {
        /* ERR: Risk of code running away */
        return 0xFFFFFFFF;
    }

    /* Verify Address, No flash operation if the address is out of range */
    if (((read_addr >= start_address) && (read_addr <= end_address))) {
        for (i = 0; i < read_len; i++) {
            if (FLASH_ReadByte (read_addr) != buff[i]) {
                /* To prevent 0-length errors, the returned position +1 */
                return i + 1;
            }
            read_addr++;
        }
    }

    return 0;
}

/*********************************************************************
 * @fn      IAP_Flash_Program
 *
 * @brief   IAP programming code, including write and verify,
 *          Specify length & address, Based On Fast Flash Operation,
 *          With address protection and program runaway protection.
 *
 * @return  ret : The meaning of 'ret' can be found in the notes of the
 *          corresponding function.
 */
uint32_t IAP_Flash_Program (uint32_t address, uint8_t *buff, uint32_t length) {
    uint32_t ret;
    /* IAP Write */
    Flash_Operation_Key1 = DEF_FLASH_OPERATION_KEY_CODE_1;
    ret = IAP_Flash_Write (address, buff, length);
    Flash_Operation_Key1 = 0;
    if (ret != 0) {
        return ret;
    }
    /* IAP Verify */
    Flash_Operation_Key1 = DEF_FLASH_OPERATION_KEY_CODE_1;
    ret = IAP_Flash_Verify (address, buff, length);
    Flash_Operation_Key1 = 0;
    if (ret != 0) {
        return ret;
    }

    return ret;
}

/*********************************************************************
 * @fn      mStopIfError
 *
 * @brief   Checking the operation status, displaying the error code and stopping if there is an error
 *          input : iError - Error code input
 *
 * @return  none
 */
void mStopIfError (uint8_t iError) {
    if (iError == ERR_SUCCESS) {
        /* operation success, return */
        return;
    }
    appendString (&scb, "ERROR:");
    char error[5];
    intToString (iError, error);
    appendString (&scb, error);
    while (1) { }
}

void MapperCode_Write (CartType type, uint32_t cartSize, char *Filename) {
    uint32_t cfg_address = 0x08007F00;
    uint16_t volatile cartSizekB = ((cartSize + 512) / 1024);
    uint32_t cfg[64];

    cfg[0] = type;
    cfg[1] = cartSizekB;

    strcpy ((char *)&cfg[2], Filename);

    // Copy data to flash.
    FLASH_Unlock_Fast();
    FLASH_ErasePage_Fast (cfg_address);
    FLASH_ProgramPage_Fast (cfg_address, cfg);
    FLASH_Lock_Fast();
}

void MapperCode_Update (CartType type) {
    uint32_t cfg_address = 0x08007F00;
    uint32_t cfg[64];


    // Create a pointer to the specified memory address
    uint32_t *src_ptr = (uint32_t *)cfg_address;

    // Copy 64 uint32_t values from the memory address to the cfg array
    for (int i = 0; i < 64; i++) {
        cfg[i] = src_ptr[i];
    }
    // Update mapper
    cfg[0] = type;

    FLASH_Unlock_Fast();
    FLASH_ErasePage_Fast (cfg_address);
    FLASH_ProgramPage_Fast (cfg_address, cfg);
    FLASH_Lock_Fast();
}