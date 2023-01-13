// /*
//  * Copyright (c) Alex2772, https://github.com/alex2772
//  *
//  * This program is free software: you can redistribute it and/or modify
//  * it under the terms of the GNU General Public License as published by
//  * the Free Software Foundation, either version 3 of the License, or
//  * (at your option) any later version.
//  *
//  * This program is distributed in the hope that it will be useful,
//  * but WITHOUT ANY WARRANTY; without even the implied warranty of
//  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  * GNU General Public License for more details.
//  *
//  * You should have received a copy of the GNU General Public License
//  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
//  *

#include <cstdint>
#include <type_traits>
#include <cstring>
#include <optional>
#include <iterator>
#include <stm32f4xx_hal.h>
#include <cassert>
#include <array>

static constexpr std::size_t operator""_kilobytes(unsigned long long i) {
    return i * 1024;
}

static constexpr std::size_t STORAGE_SIZE = 192_kilobytes;
static constexpr std::size_t TEMPORARY_BUFFER_SIZE = 16_kilobytes;

__attribute__((__section__(".user_data"))) std::array<std::uint8_t, STORAGE_SIZE> gData;

static std::size_t addressToSector(std::uintptr_t address) {
    return (address - 0x800'0000) / 0x4000;
}

struct RamBuffer {
    std::size_t sectorBeginAddress;
    std::uint8_t buffer[TEMPORARY_BUFFER_SIZE];

    explicit RamBuffer(std::size_t initialWriteAddress) {
        sectorBeginAddress = sectorBeginAddressOf(initialWriteAddress);
        std::memcpy(buffer, gData.data() + sectorBeginAddress, sizeof(buffer));
    }

    static std::size_t sectorBeginAddressOf(std::size_t address) {
        return (address / sizeof(buffer)) * sizeof(buffer);
    }
};
std::optional<RamBuffer> temporaryBuffer;



static void flushBuffer() {
    auto sourceBegin = reinterpret_cast<std::uint32_t*>(std::begin(temporaryBuffer->buffer));
    auto sourceEnd = sourceBegin + (sizeof(temporaryBuffer->buffer)) / sizeof(std::uint32_t);

    // unlock memory
    if (HAL_FLASH_Unlock() != HAL_OK) {
        assert(0);
    }

    const auto sector = addressToSector(reinterpret_cast<std::uintptr_t>(&gData) + temporaryBuffer->sectorBeginAddress);
    FLASH_Erase_Sector(sector, FLASH_VOLTAGE_RANGE_3);

    for (std::uint32_t dst = reinterpret_cast<std::uint32_t>(&gData) + temporaryBuffer->sectorBeginAddress, *src = sourceBegin; src != sourceEnd; ++src, dst += sizeof(std::uint32_t)) {
        HAL_FLASH_Program(TYPEPROGRAM_WORD, dst, *src);
    }

    HAL_FLASH_Lock();

    temporaryBuffer.reset();
}

static void updateLED() {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, HAL_GetTick() / 50 % 2 ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

extern "C" int mystorage_write(std::size_t address, const std::uint8_t* data, std::size_t size) {
    updateLED();

    if (address + size > sizeof(gData)) {
        return -1;
    }

    if (temporaryBuffer) {
        if (temporaryBuffer->sectorBeginAddress != RamBuffer::sectorBeginAddressOf(address)) {
            flushBuffer();
            temporaryBuffer.emplace(address);
        }
    } else {
        temporaryBuffer.emplace(address);
    }
    std::memcpy(temporaryBuffer->buffer + (address - temporaryBuffer->sectorBeginAddress), data, size);

    return 0;
}

extern "C" int mystorage_read(std::size_t address, std::uint8_t* data, std::size_t size) {
    updateLED();

    if (address + size > sizeof(gData)) {
        return -1;
    }

    if (temporaryBuffer) {
        flushBuffer();
    }

    std::memcpy(data, reinterpret_cast<const std::uint8_t*>(&gData) + address, size);
    return 0;
}
