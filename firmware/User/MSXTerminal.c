#include "MSXTerminal.h"
#include "utils.h"
#include "cart.h"
#include "usb_host_iap.h"


CircularBuffer scb;
CircularBuffer icb;

void Init_MSXTerminal (void) {

    initBuffer (&scb);
    initMiniBuffer (&icb);
    IAP_Initialization();
    while (MountDrive() == 0) { };
    ListFiles();
}