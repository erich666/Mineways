#pragma once
static int gLocOK;

static int gX, gZ;

class Location
{
    Location(void);
    ~Location(void);
};

void getLocationData(int& x, int& z);
void setLocationData(int x, int z);
int doLocation(HINSTANCE hInst, HWND hWnd);


