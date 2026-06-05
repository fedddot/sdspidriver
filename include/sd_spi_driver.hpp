#ifndef	SD_SPI_DRIVER_HPP
#define	SD_SPI_DRIVER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>

namespace sdspidriver {
    /// @brief SPI-mode SD card driver supporting SD v1, SD v2, and MMC cards.
    class SdSpiDriver {
    public:
        enum: std::size_t {
            BLOCK_SIZE = 512UL
        };

        /// @brief State of the SPI chip-select line.
        enum class ChipSelectState: int {
            SELECTED = 0,
            UNSELECTED = 1
        };

        /// @brief Callback that initialises the SPI peripheral.
        using SpiInit = std::function<void(void)>;
        /// @brief Callback that sets the SPI clock frequency in Hz.
        using SetSpiSpeed = std::function<void(const std::uint32_t speed_hz)>;
        /// @brief Callback that transmits one byte and returns the received byte.
        using TrancieveByte = std::function<std::uint8_t(const std::uint8_t byte)>;
        /// @brief Callback that drives the chip-select line to the given state.
        using ChipSelector = std::function<void(const ChipSelectState state)>;

        /// @brief Constructs and initialises an SD card driver over SPI.
        /// @param spi_init Callback invoked once to initialise the SPI peripheral.
        /// @param set_spi_speed Callback used to change the SPI clock frequency.
        /// @param trancieve_byte Callback that sends and receives a single byte.
        /// @param chip_selector Callback that controls the chip-select line.
        /// @param max_r1_receive_attempts Maximum polling attempts when waiting for an R1 response byte.
        /// @param max_idle_data_cycles Maximum dummy-clock cycles sent while waiting for the card to become ready.
        /// @param low_spi_speed_hz SPI clock frequency used during card initialisation.
        /// @param high_spi_speed_hz SPI clock frequency used for normal data transfers.
        SdSpiDriver(
            const SpiInit& spi_init,
            const SetSpiSpeed& set_spi_speed,
            const TrancieveByte& trancieve_byte,
            const ChipSelector& chip_selector,
            const std::uint32_t max_r1_receive_attempts = 100UL,
            const std::uint32_t max_idle_data_cycles = 500000UL,
            const std::uint32_t low_spi_speed_hz = 400000UL,
            const std::uint32_t high_spi_speed_hz = 25000000UL
        ): m_spi_init(spi_init), m_set_spi_speed(set_spi_speed), m_trancieve_byte(trancieve_byte), m_chip_selector(chip_selector), m_max_r1_receive_attempts(max_r1_receive_attempts), m_max_idle_data_cycles(max_idle_data_cycles), m_low_spi_speed_hz(low_spi_speed_hz), m_high_spi_speed_hz(high_spi_speed_hz) {
            if (!m_spi_init || !m_set_spi_speed || !m_trancieve_byte || !m_chip_selector) {
                throw std::invalid_argument("invalid callback function");
            }
            if (m_low_spi_speed_hz == 0 || m_high_spi_speed_hz == 0) {
                throw std::invalid_argument("SPI speed must be greater than 0");
            }
            if (m_max_r1_receive_attempts == 0 || m_max_idle_data_cycles == 0) {
                throw std::invalid_argument("max attempts must be greater than 0");
            }
            m_spi_init();
            init_card();
            m_set_spi_speed(m_high_spi_speed_hz);
        }
        SdSpiDriver(const SdSpiDriver&) = default;
        SdSpiDriver& operator=(const SdSpiDriver&) = delete;
        SdSpiDriver(SdSpiDriver&&) = default;
        SdSpiDriver& operator=(SdSpiDriver&&) = delete;
        ~SdSpiDriver() noexcept = default;
        
        /// @brief Reads one 512-byte block from the card.
        /// @param block_address Zero-based block index to read.
        /// @return The raw 512-byte contents of the requested block.
        std::array<std::uint8_t, BLOCK_SIZE> read_block(const std::uint32_t block_address) const;

        /// @brief Writes one 512-byte block to the card.
        /// @param block_address Zero-based block index to write.
        /// @param data 512-byte buffer whose contents are written to the card.
        void write_block(const std::uint32_t block_address, const std::array<std::uint8_t, BLOCK_SIZE>& data) const;

        /// @brief Returns the total number of 512-byte blocks on the card.
        /// @return Total block count as reported by the card's CSD register.
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

        // Callbacks
        SpiInit m_spi_init;
        SetSpiSpeed m_set_spi_speed;
        TrancieveByte m_trancieve_byte;
        ChipSelector m_chip_selector;
        SdType m_sd_type;
        AddressMode m_address_mode;
        std::uint64_t m_total_blocks;
        
        // Params
        const std::size_t m_max_r1_receive_attempts;
        const std::uint32_t m_max_idle_data_cycles;
        const std::uint32_t m_low_spi_speed_hz;
        const std::uint32_t m_high_spi_speed_hz;

        void init_card();
        void init_sd_v1();
        void init_sd_v2();
        void read_csd_sdhc_sdxc();
        void release_card(const std::uint32_t attempts) const;
        static std::uint32_t calculate_address(const std::uint32_t block_address, const AddressMode address_mode);
        static std::uint8_t calculate_crc(const SdCommand cmd, const std::uint32_t arg);

        template <std::size_t Nresp>
        std::array<std::uint8_t, Nresp> send_command(const SdCommand cmd, const std::uint32_t arg, const std::size_t read_attempts) const {
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
    };
}

#endif // SD_SPI_DRIVER_HPP