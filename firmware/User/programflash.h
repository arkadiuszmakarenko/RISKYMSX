#include "cart.h"
#include "ff.h"
#include "MSXTerminal.h"


/* Flash Operation Key Setting */
#define DEF_FLASH_OPERATION_KEY_CODE_0 0x1A86FF00 /* IAP Flash operation Key-code 0 */
#define DEF_FLASH_OPERATION_KEY_CODE_1 0x55AA55AA /* IAP Flash operation Key-code 1 */

/* IAP Load buffer Definitions */
#define DEF_MAX_IAP_BUFFER_LEN 512 /* IAP Load buffer size */

#define DEF_COM_BUF_LEN 255

/* Flash page size */
#define DEF_FLASH_PAGE_SIZE 0x100 /* Flash Page size, refer to the data-sheet (ch32vf2x_3xRM.pdf) for details */

extern uint8_t
FLASH_ReadByte (uint32_t address);
extern uint16_t FLASH_ReadHalfWord (uint32_t address);
extern uint32_t FLASH_ReadWord (uint32_t address);
extern uint8_t IAP_Flash_Erase (uint32_t address, uint32_t length);
extern uint8_t IAP_Flash_Read (uint32_t address, uint8_t *buff, uint32_t length);
extern uint8_t IAP_Flash_Write (uint32_t address, uint8_t *buff, uint32_t length);
extern uint32_t IAP_Flash_Verify (uint32_t address, uint8_t *buff, uint32_t length);
extern uint32_t IAP_Flash_Program (uint32_t address, uint8_t *buff, uint32_t length);
extern void FLASH_ReadWordAdd (uint32_t address, u32 *buff, uint16_t length);

void mStopIfError (uint8_t iError);
void ProgramCart (CartType cartType, char *Filename, char *Folder);

extern void MapperCode_Write (CartType type, uint32_t cartSize, char *Filename);
extern void MapperCode_Update (CartType type);