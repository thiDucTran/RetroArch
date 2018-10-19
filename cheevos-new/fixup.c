/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2015-2018 - Andre Leiradella
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "fixup.h"
#include "cheevos.h"
#include "util.h"

#include "../retroarch.h"
#include "../core.h"

#include <rcheevos.h>

static int cheevos_cmpaddr(const void* e1, const void* e2)
{
   const cheevos_fixup_t* f1 = (const cheevos_fixup_t*)e1;
   const cheevos_fixup_t* f2 = (const cheevos_fixup_t*)e2;

   if (f1->address < f2->address)
   {
      return -1;
   }
   else if (f1->address > f2->address)
   {
      return 1;
   }
   else
   {
      return 0;
   }
}

static size_t cheevos_var_reduce(size_t addr, size_t mask)
{
   while (mask)
   {
      size_t tmp = (mask - 1) & ~mask;
      addr = (addr & tmp) | ((addr >> 1) & ~tmp);
      mask = (mask & (mask - 1)) >> 1;
   }

   return addr;
}

static size_t cheevos_var_highest_bit(size_t n)
{
   n |= n >>  1;
   n |= n >>  2;
   n |= n >>  4;
   n |= n >>  8;
   n |= n >> 16;

   return n ^ (n >> 1);
}

void cheevos_fixup_init(cheevos_fixups_t* fixups)
{
   fixups->elements = NULL;
   fixups->capacity = fixups->count = 0;
   fixups->dirty = false;
}

void cheevos_fixup_destroy(cheevos_fixups_t* fixups)
{
   CHEEVOS_FREE(fixups->elements);
   cheevos_fixup_init(fixups);
}

const uint8_t* cheevos_fixup_find(cheevos_fixups_t* fixups, unsigned address, int console)
{
   cheevos_fixup_t key;
   cheevos_fixup_t* found;
   const uint8_t* location;

   if (fixups->dirty)
   {
      qsort(fixups->elements, fixups->count, sizeof(cheevos_fixup_t), cheevos_cmpaddr);
      fixups->dirty = false;
   }

   key.address = address;
   found = (cheevos_fixup_t*)bsearch(&key, fixups->elements, fixups->count, sizeof(cheevos_fixup_t), cheevos_cmpaddr);

   if (found != NULL)
   {
      return found->location;
   }

   if (fixups->count == fixups->capacity)
   {
      unsigned new_capacity = fixups->capacity == 0 ? 16 : fixups->capacity * 2;
      cheevos_fixup_t* new_elements = (cheevos_fixup_t*)
         realloc(fixups->elements, new_capacity * sizeof(cheevos_fixup_t));

      if (new_elements == NULL)
      {
         return NULL;
      }

      fixups->elements = new_elements;
      fixups->capacity = new_capacity;
   }

   fixups->elements[fixups->count].address = address;
   fixups->elements[fixups->count++].location = location =
      cheevos_patch_address(address, console);
   fixups->dirty = true;

   return location;
}

const uint8_t* cheevos_patch_address(unsigned address, int console)
{
   rarch_system_info_t* system = runloop_get_system_info();
   const void* pointer = NULL;

   if (console == RC_CONSOLE_NINTENDO)
   {
      if (address >= 0x0800 && address < 0x2000)
      {
         /* Address in the mirrorred RAM, adjust to real RAM. */
         CHEEVOS_LOG(CHEEVOS_TAG "NES memory address in mirrorred RAM %X, adjusted to %X\n", address, address & 0x07ff);
         address &= 0x07ff;
      }
   }
   else if (console == RC_CONSOLE_GAMEBOY_COLOR)
   {
      if (address >= 0xe000 && address <= 0xfdff)
      {
         /* Address in the echo RAM, adjust to real RAM. */
         CHEEVOS_LOG(CHEEVOS_TAG "GBC memory address in echo RAM %X, adjusted to %X\n", address, address - 0x2000);
         address -= 0x2000;
      }
   }

   if (system->mmaps.num_descriptors != 0)
   {
      /* We have memory descriptors, use it. */
      const rarch_memory_descriptor_t* desc = NULL;
      const rarch_memory_descriptor_t* end  = NULL;

      /* Patch the address to correctly map it to the mmaps. */
      if (console == RC_CONSOLE_GAMEBOY_ADVANCE)
      {
         if (address < 0x8000)
         {
            /* Internal RAM. */
            CHEEVOS_LOG(CHEEVOS_TAG "GBA memory address %X adjusted to %X\n", address, address + 0x3000000);
            address += 0x3000000;
         }
         else
         {
            /* Work RAM. */
            CHEEVOS_LOG(CHEEVOS_TAG "GBA memory address %X adjusted to %X\n", address, address + 0x2000000 - 0x8000);
            address += 0x2000000 - 0x8000;
         }
      }
      else if (console == RC_CONSOLE_PC_ENGINE)
      {
         /* RAM. */
         CHEEVOS_LOG(CHEEVOS_TAG "PCE memory address %X adjusted to %X\n", address, address + 0x1f0000);
         address += 0x1f0000;
      }
      else if (console == RC_CONSOLE_SUPER_NINTENDO)
      {
         if (address < 0x020000)
         {
            /* Work RAM. */
            CHEEVOS_LOG(CHEEVOS_TAG "SNES memory address %X adjusted to %X\n", address, address + 0x7e0000);
            address += 0x7e0000;
         }
         else
         {
            /* Save RAM. */
            CHEEVOS_LOG(CHEEVOS_TAG "SNES memory address %X adjusted to %X\n", address, address + 0x006000 - 0x020000);
            address += 0x006000 - 0x020000;
         }
      }

      desc = system->mmaps.descriptors;
      end  = desc + system->mmaps.num_descriptors;

      for (; desc < end; desc++)
      {
         if (((desc->core.start ^ address) & desc->core.select) == 0)
         {
            unsigned addr = address;
            pointer       = desc->core.ptr;
            
            address       = (unsigned)cheevos_var_reduce(
               (addr - desc->core.start) & desc->disconnect_mask,
               desc->core.disconnect);

            if (address >= desc->core.len)
               address -= cheevos_var_highest_bit(address);

            address += desc->core.offset;

            CHEEVOS_LOG(CHEEVOS_TAG "address %X set to descriptor %d at offset %X\n", addr, (int)((desc - system->mmaps.descriptors) + 1), address);
            break;
         }
      }
   }
   else
   {
      unsigned i;

      for (i = 0; i < 4; i++)
      {
         retro_ctx_memory_info_t meminfo;

         switch (i)
         {
            case 0:
               meminfo.id = RETRO_MEMORY_SYSTEM_RAM;
               break;
            case 1:
               meminfo.id = RETRO_MEMORY_SAVE_RAM;
               break;
            case 2:
               meminfo.id = RETRO_MEMORY_VIDEO_RAM;
               break;
            case 3:
               meminfo.id = RETRO_MEMORY_RTC;
               break;
         }

         core_get_memory(&meminfo);

         if (address < meminfo.size)
         {
            pointer = meminfo.data;
            break;
         }

         /**
          * HACK Subtract the correct amount of bytes to reach the save RAM as
          * it's size is not always set correctly in the core.
          */
         if (i == 0 && console == RC_CONSOLE_NINTENDO)
            address -= 0x6000;
         else
            address -= meminfo.size;
      }
   }

   if (pointer == NULL)
   {
      return NULL;
   }

   return (const uint8_t*)pointer + address;
}
