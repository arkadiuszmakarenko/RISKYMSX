#ifndef __MSXTerminal_H
#define __MSXTerminal_H

typedef enum page {
    MAIN,
    MAPPER

} MenuType;

void Init_MSXTerminal (void);

void PrintMainMenu (int page);
void PrintMapperMenu();
void ProcessMSXTerminal (void);

#endif /* __MSXTerminal_H */
