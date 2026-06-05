#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "sd_spi_driver.hpp"

using namespace sd_spi_driver;

std::array<std::uint8_t, SdSpiDriver::BLOCK_SIZE> SdSpiDriver::read_block(const std::uint32_t block_address) const {
    release_card(500000UL);
    const auto address = calculate_address(block_address, m_address_mode);
    const auto cmd17_response = send_command<1>(SdCommand::CMD17, address, RESPONSE_MAX_ATTEMPTS);
    if (cmd17_response[0] != 0x00) {
        throw std::runtime_error("Failed to read SD card block: CMD17 did not return expected response");
    }
    enum: std::uint64_t { DATA_TOKEN_MAX_ATTEMPTS = 500000UL };
    auto attempts_remaining = std::uint64_t(DATA_TOKEN_MAX_ATTEMPTS);
    auto data_token = std::uint8_t(0xFF);
    while (attempts_remaining) {
        data_token = m_trancieve_byte(0xFF);
        if (data_token == 0xFE) {
            break;
        }
        --attempts_remaining;
    }
    if (data_token != 0xFE) {
        throw std::runtime_error("Failed to read SD card block: did not receive expected data token");
    }
    std::array<std::uint8_t, BLOCK_SIZE> block_data;
    for (std::size_t i = 0; i < BLOCK_SIZE; ++i) {
        block_data[i] = m_trancieve_byte(0xFF);
    }
    // Discard CRC
    m_trancieve_byte(0xFF);
    m_trancieve_byte(0xFF);

    return block_data;
}

void SdSpiDriver::write_block(const std::uint32_t block_address, const std::array<std::uint8_t, SdSpiDriver::BLOCK_SIZE>& data) const {
    release_card(500000UL);
    const auto address = calculate_address(block_address, m_address_mode);
    const auto cmd24_response = send_command<1>(SdCommand::CMD24, address, RESPONSE_MAX_ATTEMPTS);
    if (cmd24_response[0] != 0x00) {
        throw std::runtime_error("Failed to write SD card block: CMD24 did not return expected response");
    }
    m_trancieve_byte(0xFE); // Data token for single block write
    for (std::size_t i = 0; i < BLOCK_SIZE; ++i) {
        m_trancieve_byte(data[i]);
    }
    // Fake CRC
    m_trancieve_byte(0xFF);
    m_trancieve_byte(0xFF);
    
    enum: std::uint64_t { DATA_PROCESSED_CYCLES = 500000UL };
    auto cycles_remaining = std::uint64_t(DATA_PROCESSED_CYCLES);
    auto status = std::uint8_t(0xFF);
    while (cycles_remaining) {
        status = m_trancieve_byte(0xFF);
        if (status != 0) {
            break;
        }
        --cycles_remaining;
    }
    if (status == 0) {
        throw std::runtime_error("The SD card did not accept the data during " + std::to_string(DATA_PROCESSED_CYCLES) + " cycles after the block data was sent");
    }
}

void SdSpiDriver::init_card() {
    m_set_spi_speed(SpiSpeed::LOW_SPEED);
    m_chip_selector(ChipSelectState::UNSELECTED);
    for (std::size_t i = 0; i < 10; ++i) {
        m_trancieve_byte(0xFF);
    }

    m_chip_selector(ChipSelectState::SELECTED);

    const auto cmd0_response = send_command<1>(SdCommand::CMD0, 0, RESPONSE_MAX_ATTEMPTS);
    if (cmd0_response[0] != 1) {
        throw std::runtime_error("Failed to initialize SD card: CMD0 did not return expected response");
    }

    const auto cmd8_response = send_command<5>(SdCommand::CMD8, 0x000001AA, RESPONSE_MAX_ATTEMPTS);
    if (1 != cmd8_response[0]) {
        init_sd_v1();
        return;
    }
    if ((cmd8_response[3] == 0x01) && (cmd8_response[4] == 0xAA)) {
        init_sd_v2();
        return;
    }
    throw std::runtime_error("Unsupported SD card type");
}

void SdSpiDriver::init_sd_v1() {
    m_sd_type = SdType::SD1;
    m_address_mode = AddressMode::BYTE_ADDRESSING;
    throw std::runtime_error("NOT IMPLEMENTED YET");
}

void SdSpiDriver::init_sd_v2() {
    m_sd_type = SdType::SD2;
    std::array<std::uint8_t, 1> acmd41_response;
    std::size_t attempt = 10000UL;
    while (attempt--) {
        const auto cmd55_response = send_command<1>(SdCommand::CMD55, 0, RESPONSE_MAX_ATTEMPTS);
        if (cmd55_response[0] > 1) {
            continue;
        }
        acmd41_response = send_command<1>(SdCommand::ACMD41, 0x40000000, RESPONSE_MAX_ATTEMPTS);
        if (acmd41_response[0] == 0x00) {
            break;
        }
        continue;
    }
    if (acmd41_response[0] != 0x00) {
        throw std::runtime_error("Failed to initialize SD card: ACMD41 did not return expected response");
    }
    const auto cmd58_response = send_command<5>(SdCommand::CMD58, 0, RESPONSE_MAX_ATTEMPTS);
    if (cmd58_response[0] != 0x00) {
        throw std::runtime_error("Failed to initialize SD card: CMD58 did not return expected response");
    }
    const auto ocr = cmd58_response[1];
    if (ocr & 0x40) {
        m_address_mode = AddressMode::BLOCK_ADDRESSING;
    } else {
        m_address_mode = AddressMode::BYTE_ADDRESSING;
    }
    const auto cmd16_response = send_command<1>(SdCommand::CMD16, BLOCK_SIZE, RESPONSE_MAX_ATTEMPTS);
    if (cmd16_response[0] != 0x00) {
        throw std::runtime_error("Failed to initialize SD card: CMD16 did not return expected response");
    }
    read_csd_sdhc_sdxc();
}

void SdSpiDriver::read_csd_sdhc_sdxc() {
    // The response length: R1 (R1_RESPONSE_LENGTH) + Some data tokens (DATA_TOKEN_MAX_LENGTH) which will be discarded + R2 (CSD_LENGTH)
    enum: std::size_t {
        R1_RESPONSE_LENGTH = 1,
        DATA_TOKEN_MAX_LENGTH = 5,
        CSD_LENGTH = 16,
        RESPONSE_LENGTH = R1_RESPONSE_LENGTH + DATA_TOKEN_MAX_LENGTH + CSD_LENGTH,
    };
    const auto cmd9_response = send_command<RESPONSE_LENGTH>(SdCommand::CMD9, 0, RESPONSE_MAX_ATTEMPTS);
    if (cmd9_response[0] != 0x00) {
        throw std::runtime_error("Failed to read SD card CSD register: CMD9 did not return expected response");
    }
    auto csd_start_index = std::size_t(1);
    while (cmd9_response[csd_start_index] == 0xFF && csd_start_index < RESPONSE_LENGTH) {
        ++csd_start_index;
    }
    if (csd_start_index >= RESPONSE_LENGTH - CSD_LENGTH) {
        throw std::runtime_error("Bad CSD data");
    }
    const auto *csd = cmd9_response.data() + csd_start_index;
    std::uint64_t device_size;
    device_size = static_cast<std::uint64_t>(csd[8]) & std::uint64_t(0x3F);
    device_size <<= 8;
    device_size |= static_cast<std::uint64_t>(csd[9]) & std::uint64_t(0xFF);
    device_size <<= 8;
    device_size |= static_cast<std::uint64_t>(csd[10]) & std::uint64_t(0xFF);
    device_size = (device_size + 1) * 512 * 1024;
    m_total_blocks = device_size / BLOCK_SIZE;
}

void SdSpiDriver::release_card(const std::uint32_t attempts) const {
    auto cycles_remaining = attempts;
    std::uint8_t byte = 0xFF;
    while (cycles_remaining) {
        byte = m_trancieve_byte(0xFF);
        if (0xFF == byte) {
            break;
        }
        --cycles_remaining;
    }
    if (byte != 0xFF) {
        throw std::runtime_error("Failed to release SD card: did not receive expected byte after " + std::to_string(attempts) + " attempts");
    }
}