#pragma once
// wx replacement for Win/Location.cpp
void getLocationData(int& x, int& z);
void setLocationData(int x, int z);
int  doLocation(wxWindow* parent);   // returns 1 if OK, 0 if cancelled
