#ifndef	SD_SPI_DRIVER_HPP
#define	SD_SPI_DRIVER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>

namespace sd_spi_driver {
    class SdSpiDriver {
    public:
        enum: std::size_t {
            BLOCK_SIZE = 512UL,
            RESPONSE_MAX_ATTEMPTS = 100UL
        };
        enum class ChipSelectState: int {
            SELECTED = 0,
            UNSELECTED = 1
        };
        enum class SpiSpeed: std::size_t {
            LOW_SPEED = 400000UL,
            HIGH_SPEED = 25000000UL,
        };
        using SpiInit = std::function<void(void)>;
        using SetSpiSpeed = std::function<void(const SpiSpeed speed)>;
        using TrancieveByte = std::function<std::uint8_t(const std::uint8_t byte)>;
        using Delay = std::function<void(const std::size_t ms)>;
        using ChipSelector = std::function<void(const ChipSelectState state)>;

        SdSpiDriver(
            const SpiInit& spi_init,
            const SetSpiSpeed& set_spi_speed,
            const TrancieveByte& trancieve_byte,
            const Delay& delay,
            const ChipSelector& chip_selector
        ): m_spi_init(spi_init), m_set_spi_speed(set_spi_speed), m_trancieve_byte(trancieve_byte), m_delay(delay), m_chip_selector(chip_selector) {
            if (!m_spi_init || !m_set_spi_speed || !m_trancieve_byte || !m_delay || !m_chip_selector) {
                throw std::invalid_argument("invalid argument(s) provided to SdSpiDriver constructor");
            }
            m_spi_init();
            init_card();
            m_set_spi_speed(SpiSpeed::HIGH_SPEED);
        }
        SdSpiDriver(const SdSpiDriver&) = default;
        SdSpiDriver& operator=(const SdSpiDriver&) = default;
        ~SdSpiDriver() noexcept = default;
        
        std::array<std::uint8_t, BLOCK_SIZE> read_block(const std::uint32_t block_address) const {
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
        void write_block(const std::uint32_t block_address, const std::array<std::uint8_t, BLOCK_SIZE>& data) const {
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

        std::uint64_t total_blocks() const {
            return m_total_blocks;
        }
    private:
        enum class SdType: int {
            SD1,
            SD2,
            MMC
        };
        enum class AddressMode: int {
            BYTE_ADDRESSING,
            BLOCK_ADDRESSING
        };

        enum class SdCommand: std::uint8_t {
            CMD0 = 0x40 + 0,
            CMD1 = 0x40 + 1,
            CMD8 = 0x40 + 8,
            CMD9 = 0x40 + 9,
            CMD16 = 0x40 + 16,
            CMD17 = 0x40 + 17,
            CMD24 = 0x40 + 24,
            CMD42 = 0x40 + 42,
            CMD55 = 0x40 + 55,
            CMD58 = 0x40 + 58,
            ACMD41 = 0x40 + 41
        };

        SpiInit m_spi_init;
        SetSpiSpeed m_set_spi_speed;
        TrancieveByte m_trancieve_byte;
        Delay m_delay;
        ChipSelector m_chip_selector;
        SdType m_sd_type;
        AddressMode m_address_mode;
        std::uint64_t m_total_blocks;

        static std::uint32_t calculate_address(const std::uint32_t block_address, const AddressMode address_mode) {
            switch (address_mode) {
            case AddressMode::BYTE_ADDRESSING:
                return block_address * BLOCK_SIZE;
            case AddressMode::BLOCK_ADDRESSING:
                return block_address;
            default:
                throw std::invalid_argument("invalid AddressMode provided to calculate_address");
            }
        }

        static std::uint8_t calculate_crc(const SdCommand cmd, std::uint32_t arg) {
            (void)arg;
            enum: std::uint8_t {
                DUMMY_CRC = 0x01,
                CMD0_CRC = 0x95,
                CMD8_CRC = 0x87
            };
            switch (cmd) {
            case SdCommand::CMD0:
                return CMD0_CRC;
            case SdCommand::CMD8:
                return CMD8_CRC;
            default:
                return DUMMY_CRC;
            }
        }

        template <std::size_t Nresp>
        std::array<std::uint8_t, Nresp> send_command(const SdCommand cmd, std::uint32_t arg, const std::size_t read_attempts) const {
            m_chip_selector(ChipSelectState::UNSELECTED);
            m_trancieve_byte(0xFF);
            m_chip_selector(ChipSelectState::SELECTED);
            m_trancieve_byte(0xFF);

            m_trancieve_byte(static_cast<std::uint8_t>(cmd));
            m_trancieve_byte((std::uint8_t)(arg >> 24));
            m_trancieve_byte((std::uint8_t)(arg >> 16));
            m_trancieve_byte((std::uint8_t)(arg >> 8 ));
            m_trancieve_byte((std::uint8_t)(arg >> 0 ));
            m_trancieve_byte(calculate_crc(cmd, arg));

            std::array<std::uint8_t, Nresp> response;
            if (Nresp == 0) {
                return response;
            }
            for (std::size_t attempt = 0; attempt < read_attempts; ++attempt) {
                const auto byte = m_trancieve_byte(0xFF);
                if ((byte & 0x80) == 0) {
                    response[0] = byte;
                    break;
                }
            }
            for (std::size_t n = 1; n < Nresp; n++) {
                response[n] = m_trancieve_byte(0xFF);
            }
            return response;
        }

        void init_card() {
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

        void init_sd_v1() {
            m_sd_type = SdType::SD1;
            m_address_mode = AddressMode::BYTE_ADDRESSING;
            throw std::runtime_error("NOT IMPLEMENTED YET");
        }

        void init_sd_v2() {
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

        void read_csd_sdhc_sdxc() {
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

        void release_card(const std::uint32_t attempts) const {
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
    };
}

#endif // SD_SPI_DRIVER_HPP