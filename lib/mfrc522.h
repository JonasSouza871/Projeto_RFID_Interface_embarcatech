/*
 * mfrc522.h
 *
 * MFRC522 library for Raspberry Pi Pico C/C++ SDK
 *
 * Created: 2023
 * Author: Benjamin Modica
 *
 * This is an adaptation of a C library for PLCOpen made by:
 * https://github.com/luisfg30/rfid
 * Which is an adaptation of the C++ Arduino library, available at:
 * https://github.com/miguelbalboa/rfid.git
 *
 * The original library is released to the public domain.
 */

#ifndef MFRC522_h
#define MFRC522_h

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

/*******************************************************************************
 * Configuration Constants
 ******************************************************************************/

// Send only one byte per transfer (see WriteRegister functions)
#define BUFFER_SIZE  1

// SPI bit rate defined as 4MHz in the original library
#define MFRC522_BIT_RATE 4000000

// Maximum number of ADT object allocations
#define MFRC_MAX_INSTANCES 2

// GPIO pin assignments for MFRC522
#define RESET_PIN 0               // GP0 - Reset

static const uint cs_pin = 5;     // GP5 - Chip Select (SDA)
static const uint sck_pin = 2;    // GP2 - SPI Clock
static const uint mosi_pin = 3;   // GP3 - SPI Master Out Slave In
static const uint miso_pin = 4;   // GP4 - SPI Master In Slave Out

// Size of the MFRC522 FIFO buffer
static const uint8_t FIFO_SIZE = 64;

// Self-test expected output bytes (MFRC522 firmware verification)
static const uint8_t SELF_TEST_BYTES[] = {
	0x00, 0xEB, 0x66, 0xBA, 0x57, 0xBF, 0x23, 0x95,
	0xD0, 0xE3, 0x0D, 0x3D, 0x27, 0x89, 0x5C, 0xDE,
	0x9D, 0x3B, 0xA7, 0x00, 0x21, 0x5B, 0x89, 0x82,
	0x51, 0x3A, 0xEB, 0x02, 0x0C, 0xA5, 0x00, 0x49,
	0x7C, 0x84, 0x4D, 0xB3, 0xCC, 0xD2, 0x1B, 0x81,
	0x5D, 0x48, 0x76, 0xD5, 0x71, 0x61, 0x21, 0xA9,
	0x86, 0x96, 0x83, 0x38, 0xCF, 0x9D, 0x5B, 0x6D,
	0xDC, 0x15, 0xBA, 0x3E, 0x7D, 0x95, 0x3B, 0x2F
};

/*******************************************************************************
 * MFRC522 Register Definitions
 ******************************************************************************/

/**
 * MFRC522 registers as described in chapter 9 of the datasheet.
 * When using SPI, all addresses are shifted one bit left (section 8.1.2.3)
 */
enum PCD_Register {
	// Page 0: Command and status registers
	CommandReg       = 0x01 << 1,  // Starts and stops command execution
	ComIEnReg        = 0x02 << 1,  // Enable and disable interrupt request control bits
	DivIEnReg        = 0x03 << 1,  // Enable and disable interrupt request control bits
	ComIrqReg        = 0x04 << 1,  // Interrupt request bits
	DivIrqReg        = 0x05 << 1,  // Interrupt request bits
	ErrorReg         = 0x06 << 1,  // Error bits showing the error status of the last command
	Status1Reg       = 0x07 << 1,  // Communication status bits
	Status2Reg       = 0x08 << 1,  // Receiver and transmitter status bits
	FIFODataReg      = 0x09 << 1,  // Input and output of 64 byte FIFO buffer
	FIFOLevelReg     = 0x0A << 1,  // Number of bytes stored in the FIFO buffer
	WaterLevelReg    = 0x0B << 1,  // Level for FIFO underflow and overflow warning
	ControlReg       = 0x0C << 1,  // Miscellaneous control registers
	BitFramingReg    = 0x0D << 1,  // Adjustments for bit-oriented frames
	CollReg          = 0x0E << 1,  // Bit position of the first bit-collision detected

	// Page 1: Command registers
	ModeReg          = 0x11 << 1,  // Defines general modes for transmitting and receiving
	TxModeReg        = 0x12 << 1,  // Defines transmission data rate and framing
	RxModeReg        = 0x13 << 1,  // Defines reception data rate and framing
	TxControlReg     = 0x14 << 1,  // Controls the logical behavior of antenna driver pins TX1 and TX2
	TxASKReg         = 0x15 << 1,  // Controls the setting of the transmission modulation
	TxSelReg         = 0x16 << 1,  // Selects the internal sources for the antenna driver
	RxSelReg         = 0x17 << 1,  // Selects internal receiver settings
	RxThresholdReg   = 0x18 << 1,  // Selects thresholds for the bit decoder
	DemodReg         = 0x19 << 1,  // Defines demodulator settings
	MfTxReg          = 0x1C << 1,  // Controls some MIFARE communication transmit parameters
	MfRxReg          = 0x1D << 1,  // Controls some MIFARE communication receive parameters
	SerialSpeedReg   = 0x1F << 1,  // Selects the speed of the serial UART interface

	// Page 2: Configuration registers
	CRCResultRegH    = 0x21 << 1,  // Shows the MSB values of the CRC calculation
	CRCResultRegL    = 0x22 << 1,  // Shows the LSB values of the CRC calculation
	ModWidthReg      = 0x24 << 1,  // Controls the ModWidth setting
	RFCfgReg         = 0x26 << 1,  // Configures the receiver gain
	GsNReg           = 0x27 << 1,  // Selects the conductance of antenna driver pins TX1 and TX2 for modulation
	CWGsPReg         = 0x28 << 1,  // Defines the conductance of the p-driver output during periods of no modulation
	ModGsPReg        = 0x29 << 1,  // Defines the conductance of the p-driver output during periods of modulation
	TModeReg         = 0x2A << 1,  // Defines settings for the internal timer
	TPrescalerReg    = 0x2B << 1,  // The lower 8 bits of the TPrescaler value (4 high bits are in TModeReg)
	TReloadRegH      = 0x2C << 1,  // Defines the 16-bit timer reload value (high byte)
	TReloadRegL      = 0x2D << 1,  // Defines the 16-bit timer reload value (low byte)
	TCounterValueRegH = 0x2E << 1, // Shows the 16-bit timer value (high byte)
	TCounterValueRegL = 0x2F << 1, // Shows the 16-bit timer value (low byte)

	// Page 3: Test registers
	TestSel1Reg      = 0x31 << 1,  // General test signal configuration
	TestSel2Reg      = 0x32 << 1,  // General test signal configuration
	TestPinEnReg     = 0x33 << 1,  // Enables pin output driver on pins D1 to D7
	TestPinValueReg  = 0x34 << 1,  // Defines the values for D1 to D7 when used as an I/O bus
	TestBusReg       = 0x35 << 1,  // Shows the status of the internal test bus
	AutoTestReg      = 0x36 << 1,  // Controls the digital self-test
	VersionReg       = 0x37 << 1,  // Shows the software version
	AnalogTestReg    = 0x38 << 1,  // Controls the pins AUX1 and AUX2
	TestDAC1Reg      = 0x39 << 1,  // Defines the test value for TestDAC1
	TestDAC2Reg      = 0x3A << 1,  // Defines the test value for TestDAC2
	TestADCReg       = 0x3B << 1   // Shows the value of ADC I and Q channels
};

/*******************************************************************************
 * MFRC522 Command Definitions
 ******************************************************************************/

/**
 * MFRC522 commands as described in chapter 10 of the datasheet.
 */
typedef enum _PCD_Command {
	PCD_Idle             = 0x00,  // No action, cancels current command execution
	PCD_Mem              = 0x01,  // Stores 25 bytes into the internal buffer
	PCD_GenerateRandomID = 0x02,  // Generates a 10-byte random ID number
	PCD_CalcCRC          = 0x03,  // Activates the CRC coprocessor or performs a self-test
	PCD_Transmit         = 0x04,  // Transmits data from the FIFO buffer
	PCD_NoCmdChange      = 0x07,  // No command change (can modify CommandReg without affecting command)
	PCD_Receive          = 0x08,  // Activates the receiver circuits
	PCD_Transceive       = 0x0C,  // Transmits data from FIFO and automatically activates receiver
	PCD_MFAuthent        = 0x0E,  // Performs the MIFARE standard authentication as a reader
	PCD_SoftReset        = 0x0F,  // Resets the MFRC522
	PCD_EnableSelfTest   = 0x09   // Enables self-test mode
} PCD_Command;

/**
 * MFRC522 RxGain[2:0] masks - defines the receiver's signal voltage gain factor.
 * Described in section 9.3.3.6 / table 98 of the datasheet.
 */
typedef enum _PCD_RxGain {
	RxGain_18dB    = 0x00 << 4,  // 000b - 18 dB, minimum
	RxGain_23dB    = 0x01 << 4,  // 001b - 23 dB
	RxGain_18dB_2  = 0x02 << 4,  // 010b - 18 dB (duplicate of 000b)
	RxGain_23dB_2  = 0x03 << 4,  // 011b - 23 dB (duplicate of 001b)
	RxGain_33dB    = 0x04 << 4,  // 100b - 33 dB, average, and typical default
	RxGain_38dB    = 0x05 << 4,  // 101b - 38 dB
	RxGain_43dB    = 0x06 << 4,  // 110b - 43 dB
	RxGain_48dB    = 0x07 << 4,  // 111b - 48 dB, maximum
	RxGain_min     = 0x00 << 4,  // Convenience alias for RxGain_18dB
	RxGain_avg     = 0x04 << 4,  // Convenience alias for RxGain_33dB
	RxGain_max     = 0x07 << 4   // Convenience alias for RxGain_48dB
} PCD_RxGain;

/*******************************************************************************
 * PICC (Proximity Integrated Circuit Card) Definitions
 ******************************************************************************/

/**
 * Commands sent to the PICC.
 */
typedef enum _PICC_Command {
	// ISO 14443-3, Type A commands for managing communication with PICCs
	PICC_CMD_REQA           = 0x26,  // REQuest command, Type A (invites PICCs in IDLE state)
	PICC_CMD_WUPA           = 0x52,  // Wake-UP command, Type A (invites PICCs in IDLE/HALT state)
	PICC_CMD_CT             = 0x88,  // Cascade Tag (used during anti-collision)
	PICC_CMD_SEL_CL1        = 0x93,  // Anti-collision/Select, Cascade Level 1
	PICC_CMD_SEL_CL2        = 0x95,  // Anti-collision/Select, Cascade Level 2
	PICC_CMD_SEL_CL3        = 0x97,  // Anti-collision/Select, Cascade Level 3
	PICC_CMD_HLTA           = 0x50,  // HaLT command, Type A (instructs ACTIVE PICC to HALT state)

	// MIFARE Classic commands
	PICC_CMD_MF_AUTH_KEY_A  = 0x60,  // Perform authentication with Key A
	PICC_CMD_MF_AUTH_KEY_B  = 0x61,  // Perform authentication with Key B
	PICC_CMD_MF_READ        = 0x30,  // Reads one 16 byte block from authenticated sector
	PICC_CMD_MF_WRITE       = 0xA0,  // Writes one 16 byte block to authenticated sector
	PICC_CMD_MF_DECREMENT   = 0xC0,  // Decrements the contents of a block
	PICC_CMD_MF_INCREMENT   = 0xC1,  // Increments the contents of a block
	PICC_CMD_MF_RESTORE     = 0xC2,  // Reads the contents of a block into internal data register
	PICC_CMD_MF_TRANSFER    = 0xB0,  // Writes internal data register contents to a block

	// MIFARE Ultralight commands
	PICC_CMD_UL_WRITE       = 0xA2   // Writes one 4 byte page to the PICC
} PICC_Command;

/**
 * MIFARE miscellaneous constants
 */
typedef enum _MIFARE_Misc {
	MF_ACK      = 0xA,  // The MIFARE Classic 4 bit ACK/NAK (any other value is NAK)
	MF_KEY_SIZE = 6     // A MIFARE Crypto1 key is 6 bytes
} MIFARE_Misc;

/**
 * PICC types that can be detected.
 * Remember to update PICC_GetTypeName() if you add more types.
 */
typedef enum _PICC_Type {
	PICC_TYPE_UNKNOWN,
	PICC_TYPE_ISO_14443_4,      // PICC compliant with ISO/IEC 14443-4
	PICC_TYPE_ISO_18092,        // PICC compliant with ISO/IEC 18092 (NFC)
	PICC_TYPE_MIFARE_MINI,      // MIFARE Classic protocol, 320 bytes
	PICC_TYPE_MIFARE_1K,        // MIFARE Classic protocol, 1KB
	PICC_TYPE_MIFARE_4K,        // MIFARE Classic protocol, 4KB
	PICC_TYPE_MIFARE_UL,        // MIFARE Ultralight or Ultralight C
	PICC_TYPE_MIFARE_PLUS,      // MIFARE Plus
	PICC_TYPE_TNP3XXX,          // Only mentioned in NXP AN 10833 MIFARE Type Identification
	PICC_TYPE_NOT_COMPLETE = 0xff  // SAK indicates UID is not complete
} PICC_Type;

/**
 * Return codes from library functions.
 * Remember to update GetStatusCodeName() if you add more codes.
 */
typedef enum _StatusCode {
	STATUS_OK,               // Success
	STATUS_ERROR,            // Error in communication
	STATUS_COLLISION,        // Collision detected
	STATUS_TIMEOUT,          // Timeout in communication
	STATUS_NO_ROOM,          // A buffer is not big enough
	STATUS_INTERNAL_ERROR,   // Internal error in the code (should not happen)
	STATUS_INVALID,          // Invalid argument
	STATUS_CRC_WRONG,        // The CRC_A does not match
	STATUS_MIFARE_NACK = 0xff  // A MIFARE PICC responded with NAK
} StatusCode;

/*******************************************************************************
 * Data Structures
 ******************************************************************************/

/**
 * A struct used for passing the UID of a PICC.
 */
typedef struct {
	uint8_t size;           // Number of bytes in the UID (4, 7, or 10)
	uint8_t uidByte[10];    // The UID bytes
	uint8_t sak;            // The SAK (Select acknowledge) byte returned from the PICC
} Uid;

/**
 * A struct used for passing a MIFARE Crypto1 key
 */
typedef struct {
	uint8_t keybyte[MF_KEY_SIZE];
} MIFARE_Key;

/**
 * A struct used to define a MFRC522 ADT (Abstract Data Type) object.
 * Useful when using more than one MFRC522 module.
 */
struct MFRC522_T {
	Uid uid;                    // Used by PICC_ReadCardSerial()
	spi_inst_t *spi;            // Select SPI0 or SPI1
	uint _chipSelectPin;        // Chip select pin
	uint8_t Tx_Buf[BUFFER_SIZE]; // Transmit buffer
	uint8_t Rx_Buf[BUFFER_SIZE]; // Receive buffer
};

// Pointer to a MFRC522 ADT object
typedef struct MFRC522_T *MFRC522Ptr_t;

/*******************************************************************************
 * Initialization Function
 ******************************************************************************/

/**
 * @brief Sets up a MFRC522 ADT object
 * @return Pointer to an initialized ADT object
 */
MFRC522Ptr_t MFRC522_Init(void);

/*******************************************************************************
 * Basic Interface Functions for Communicating with the MFRC522
 ******************************************************************************/

/**
 * @brief Writes a byte to the specified register
 */
void PCD_WriteRegister(MFRC522Ptr_t mfrc, uint8_t reg, uint8_t value);

/**
 * @brief Writes multiple bytes to the specified register
 */
void PCD_WriteNRegister(MFRC522Ptr_t mfrc, uint8_t reg, uint8_t count, uint8_t *values);

/**
 * @brief Reads a byte from the specified register
 */
uint8_t PCD_ReadRegister(MFRC522Ptr_t mfrc, uint8_t reg);

/**
 * @brief Reads multiple bytes from the specified register
 */
void PCD_ReadNRegister(MFRC522Ptr_t mfrc, uint8_t reg, uint8_t count, uint8_t *values, uint8_t rxAlign);

/**
 * @brief Sets the bits given in mask in the register
 */
void PCD_SetRegisterBitMask(MFRC522Ptr_t mfrc, uint8_t reg, uint8_t mask);

/**
 * @brief Clears the bits given in mask from the register
 */
void PCD_ClearRegisterBitMask(MFRC522Ptr_t mfrc, uint8_t reg, uint8_t mask);

/**
 * @brief Use the CRC coprocessor to calculate a CRC_A
 */
StatusCode PCD_CalculateCRC(MFRC522Ptr_t mfrc, uint8_t *data, uint8_t length, uint8_t *result);

/**
 * @brief SPI chip select control for Raspberry Pi Pico
 */
static inline void cs_select(const uint cs) {
	asm volatile("nop \n nop \n nop");
	gpio_put(cs, 0);
	asm volatile("nop \n nop \n nop");
}

static inline void cs_deselect(const uint cs) {
	asm volatile("nop \n nop \n nop");
	gpio_put(cs, 1);
	asm volatile("nop \n nop \n nop");
}

/*******************************************************************************
 * Functions for Manipulating the MFRC522
 ******************************************************************************/

/**
 * @brief Initializes the MFRC522 chip
 */
void PCD_Init(MFRC522Ptr_t mfrc, spi_inst_t *spi);

/**
 * @brief Performs a soft reset on the MFRC522 chip
 */
void PCD_Reset(MFRC522Ptr_t mfrc);

/**
 * @brief Turns the antenna on by enabling pins TX1 and TX2
 */
void PCD_AntennaOn(MFRC522Ptr_t mfrc);

/**
 * @brief Turns the antenna off by disabling pins TX1 and TX2
 */
void PCD_AntennaOff(MFRC522Ptr_t mfrc);

/**
 * @brief Gets the current MFRC522 Receiver Gain (RxGain[2:0]) value
 */
uint8_t PCD_GetAntennaGain(MFRC522Ptr_t mfrc);

/**
 * @brief Sets the MFRC522 Receiver Gain (RxGain) to value specified by given mask
 */
void PCD_SetAntennaGain(MFRC522Ptr_t mfrc, uint8_t mask);

/**
 * @brief Performs the MFRC522 self-test described in section 16.1.1 of datasheet
 * @return 0 if test passes, -1 otherwise
 */
uint8_t PCD_SelfTest(MFRC522Ptr_t mfrc);

/*******************************************************************************
 * Functions for Communicating with PICCs
 ******************************************************************************/

/**
 * @brief Executes the Transceive command
 */
StatusCode PCD_TransceiveData(MFRC522Ptr_t mfrc, uint8_t *sendData, uint8_t sendLen,
                               uint8_t *backData, uint8_t *backLen, uint8_t *validBits,
                               uint8_t rxAlign, bool checkCRC);

/**
 * @brief Transfers data to MFRC522 FIFO, executes a command, waits for completion
 */
StatusCode PCD_CommunicateWithPICC(MFRC522Ptr_t mfrc, uint8_t command, uint8_t waitIRq,
                                    uint8_t *sendData, uint8_t sendLen, uint8_t *backData,
                                    uint8_t *backLen, uint8_t *validBits, uint8_t rxAlign,
                                    bool checkCRC);

/**
 * @brief Transmits a REQuest command, Type A
 */
StatusCode PICC_RequestA(MFRC522Ptr_t mfrc, uint8_t *bufferATQA, uint8_t *bufferSize);

/**
 * @brief Transmits a Wake-UP command, Type A
 */
StatusCode PICC_WakeupA(MFRC522Ptr_t mfrc, uint8_t *bufferATQA, uint8_t *bufferSize);

/**
 * @brief Transmits REQA or WUPA commands
 */
StatusCode PICC_REQA_or_WUPA(MFRC522Ptr_t mfrc, uint8_t command, uint8_t *bufferATQA,
                              uint8_t *bufferSize);

/**
 * @brief Transmits SELECT/ANTICOLLISION commands to select a single PICC
 */
StatusCode PICC_Select(MFRC522Ptr_t mfrc, Uid *uid, uint8_t validBits);

/**
 * @brief Instructs a PICC in state ACTIVE to go to state HALT
 */
StatusCode PICC_HaltA(MFRC522Ptr_t mfrc);

/*******************************************************************************
 * Functions for Communicating with MIFARE PICCs
 ******************************************************************************/

/**
 * @brief Executes the MFRC522 MFAuthent command for MIFARE authentication
 */
StatusCode PCD_Authenticate(MFRC522Ptr_t mfrc, uint8_t command, uint8_t blockAddr,
                             MIFARE_Key *key, Uid *uid);

/**
 * @brief Used to exit the PCD from its authenticated state
 */
void PCD_StopCrypto1(MFRC522Ptr_t mfrc);

/**
 * @brief Reads 16 bytes (+ 2 bytes CRC_A) from the active PICC
 */
StatusCode MIFARE_Read(MFRC522Ptr_t mfrc, uint8_t blockAddr, uint8_t *buffer, uint8_t *bufferSize);

/**
 * @brief Writes 16 bytes to the active PICC
 */
StatusCode MIFARE_Write(MFRC522Ptr_t mfrc, uint8_t blockAddr, uint8_t *buffer, uint8_t bufferSize);

/**
 * @brief Writes a 4 byte page to the active MIFARE Ultralight PICC
 */
StatusCode MIFARE_Ultralight_Write(MFRC522Ptr_t mfrc, uint8_t page, uint8_t *buffer,
                                     uint8_t bufferSize);

/**
 * @brief MIFARE Decrement subtracts the delta from the value of the addressed block
 */
StatusCode MIFARE_Decrement(MFRC522Ptr_t mfrc, uint8_t blockAddr, long delta);

/**
 * @brief MIFARE Increment adds the delta to the value of the addressed block
 */
StatusCode MIFARE_Increment(MFRC522Ptr_t mfrc, uint8_t blockAddr, long delta);

/**
 * @brief MIFARE Restore copies the value of the addressed block into volatile memory
 */
StatusCode MIFARE_Restore(MFRC522Ptr_t mfrc, uint8_t blockAddr);

/**
 * @brief MIFARE Transfer writes the contents of the internal data register to a block
 */
StatusCode MIFARE_Transfer(MFRC522Ptr_t mfrc, uint8_t blockAddr);

/**
 * @brief Helper routine to read the current value from a Value Block
 */
StatusCode MIFARE_GetValue(MFRC522Ptr_t mfrc, uint8_t blockAddr, long *value);

/**
 * @brief Helper routine to write a specific value into a Value Block
 */
StatusCode MIFARE_SetValue(MFRC522Ptr_t mfrc, uint8_t blockAddr, long value);

/**
 * @brief Authenticate with a NTAG216
 */
StatusCode PCD_NTAG216_AUTH(MFRC522Ptr_t mfrc, uint8_t *passWord, uint8_t pACK[]);

/*******************************************************************************
 * Support Functions
 ******************************************************************************/

/**
 * @brief Wrapper for MIFARE protocol communication
 */
StatusCode PCD_MIFARE_Transceive(MFRC522Ptr_t mfrc, uint8_t *sendData, uint8_t sendLen,
                                  bool acceptTimeout);

/**
 * @brief Returns string name for a status code
 */
const char *GetStatusCodeName(StatusCode code);

/**
 * @brief Returns string name for a PICC type
 */
const char *PICC_GetTypeName(PICC_Type type);

/**
 * @brief Helper function for two-step MIFARE Classic protocol operations
 */
StatusCode MIFARE_TwoStepHelper(MFRC522Ptr_t mfrc, uint8_t command, uint8_t blockAddr, long data);

/*******************************************************************************
 * Debugging Functions
 ******************************************************************************/

/**
 * @brief Dumps debug info about the connected PCD to Serial
 */
void PCD_DumpVersionToSerial(MFRC522Ptr_t mfrc);

/**
 * @brief Dumps debug info about the selected PICC to Serial
 */
void PICC_DumpToSerial(MFRC522Ptr_t mfrc, Uid *uid);

/**
 * @brief Dumps card details (UID, SAK, Type) to Serial
 */
void PICC_DumpDetailsToSerial(Uid *uid);

/**
 * @brief Dumps memory contents of a MIFARE Classic PICC to Serial
 */
void PICC_DumpMifareClassicToSerial(MFRC522Ptr_t mfrc, Uid *uid, PICC_Type piccType,
                                     MIFARE_Key *key);

/**
 * @brief Dumps memory contents of a MIFARE Classic sector to Serial
 */
void PICC_DumpMifareClassicSectorToSerial(MFRC522Ptr_t mfrc, Uid *uid, MIFARE_Key *key,
                                           uint8_t sector);

/**
 * @brief Dumps memory contents of a MIFARE Ultralight PICC to Serial
 */
void PICC_DumpMifareUltralightToSerial(MFRC522Ptr_t mfrc);

/*******************************************************************************
 * Advanced MIFARE Functions
 ******************************************************************************/

/**
 * @brief Sets the access bits for a MIFARE Classic sector
 */
void MIFARE_SetAccessBits(uint8_t *accessBitBuffer, uint8_t g0, uint8_t g1, uint8_t g2, uint8_t g3);

/**
 * @brief Opens a UID backdoor on some MIFARE Classic cards
 */
bool MIFARE_OpenUidBackdoor(MFRC522Ptr_t mfrc, bool logErrors);

/**
 * @brief Sets a new UID on a MIFARE Classic card with UID backdoor
 */
bool MIFARE_SetUid(MFRC522Ptr_t mfrc, uint8_t *newUid, uint8_t uidSize, bool logErrors);

/**
 * @brief Unbricks a MIFARE Classic card with UID backdoor
 */
bool MIFARE_UnbrickUidSector(MFRC522Ptr_t mfrc, bool logErrors);

/*******************************************************************************
 * Convenience Functions
 ******************************************************************************/

/**
 * @brief Returns true if a new card is present
 */
bool PICC_IsNewCardPresent(MFRC522Ptr_t mfrc);

/**
 * @brief Reads the card serial number
 */
bool PICC_ReadCardSerial(MFRC522Ptr_t mfrc);

#endif // MFRC522_h
