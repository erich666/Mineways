// macOS build: stdafx.h resolves to this file first (Mac/Makefile puts "-I."
// ahead of "-I../Win"). Delegate to the real (WIN32/!WIN32-guarded) source of
// truth instead of duplicating it, so the two builds can't silently drift.
#pragma once
#include "../Win/stdafx.h"
