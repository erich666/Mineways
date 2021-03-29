// Aseprite TGA Library
// Copyright (C) 2020  Igara Studio S.A.
//
// This file is released under the terms of the MIT license.
// Read LICENSE.txt for more information.

#include "tga.h"

namespace tga {

StdioFileInterface::StdioFileInterface(FILE* file)
  : m_file(file)
  , m_ok(true)
{
}

bool StdioFileInterface::ok() const
{
  return m_ok;
}

size_t StdioFileInterface::tell()
{
  return ftell(m_file);
}

void StdioFileInterface::seek(size_t absPos)
{
  fseek(m_file, (long)absPos, SEEK_SET);
}

uint8_t StdioFileInterface::read8()
{
  int value = fgetc(m_file);
  if (value != EOF)
    return (uint8_t)value;

  m_ok = false;
  return 0;
}

void StdioFileInterface::write8(uint8_t value)
{
  fputc(value, m_file);
}

} // namespace tga
