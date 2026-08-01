#include "globals.h"
#include "serial_parser.h"
#include "serial_input_protocol.h"
#include "serial_param_protocol.h"

// RX-only Input panel protocol. Discard 'a'..'f' / 'q'; apply owned 'p'/'w' only.
// Do not call Serial1.write / availableForWrite on the Input bus.

static void aux_discard_frame(char, const uint8_t*, uint8_t) {}

static void aux_handle_param16(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_PARAM_16) return;
  ParamFrame frame;
  decode_param_p(payload, frame);
  update_parameters(frame.id, (int16_t)frame.value);
}

static void aux_handle_param8(char, const uint8_t* payload, uint8_t len) {
  if (len != INPUT_SERIAL_LEN_PARAM_8) return;
  ParamFrame frame;
  decode_param_w(payload, frame);
  update_parameters(frame.id, (int16_t)frame.value);
}

static const SerialCommandDef inputSerialCommands[] = {
  { INPUT_CMD_ADSR1_BLOCK,   INPUT_SERIAL_LEN_ADSR_BLOCK,   aux_discard_frame   },
  { INPUT_CMD_ADSR2_BLOCK,   INPUT_SERIAL_LEN_ADSR_BLOCK,   aux_discard_frame   },
  { INPUT_CMD_ADSR3_BLOCK,   INPUT_SERIAL_LEN_ADSR_BLOCK,   aux_discard_frame   },
  { INPUT_CMD_FILTER_BLOCK,  INPUT_SERIAL_LEN_FILTER_BLOCK, aux_discard_frame   },
  { INPUT_CMD_ADSR1_TO_VCA,  INPUT_SERIAL_LEN_ADSR1_TO_VCA, aux_discard_frame   },
  { INPUT_CMD_PW_VALUE,      INPUT_SERIAL_LEN_PW_VALUE,     aux_discard_frame   },
  { INPUT_CMD_PARAM_16,      INPUT_SERIAL_LEN_PARAM_16,     aux_handle_param16  },
  { INPUT_CMD_PARAM_8,       INPUT_SERIAL_LEN_PARAM_8,      aux_handle_param8   },
  { INPUT_CMD_PRESET_NAME,   INPUT_SERIAL_LEN_PRESET_NAME,  aux_discard_frame   },
};

static SerialParserContext inputSerialParser = {
  SERIAL_WAIT_FOR_CMD, 0, nullptr, {0}, 0, 0, 0
};

void init_serial() {
  // UART0 / Serial1: RX from Input TX fanout. Do not wire TX to the Input bus.
  Serial1.setFIFOSize(512);
  Serial1.setPollingMode(false);
  Serial1.setRX(INPUT_RX_PIN);
  Serial1.setTX(0);  // unused electrically
  Serial1.begin(INPUT_BAUD);
}

void serial_panel_task() {
  if (inputSerialParser.state == SERIAL_READ_PAYLOAD) {
    serial_parser_check_timeout(inputSerialParser, micros());
  }
  while (Serial1.available() > 0) {
    uint8_t b = Serial1.read();
    serial_parser_process_byte(
      inputSerialParser,
      inputSerialCommands,
      sizeof(inputSerialCommands) / sizeof(inputSerialCommands[0]),
      b,
      micros()
    );
  }
}
