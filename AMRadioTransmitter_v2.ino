// ################################################################################################################################# 
// ##           ESP32 - AM RADIO TRANSMITTER v2, by PPete
// ##          ===========================================
// ##
// ##  - Github repo: https://github.com/ppete2/ESP32_AMRadioTransmitter_v2
// ##  - Idea and original code: "AM Radio Transmitter" by bitluni https://youtube.com/bitlunislab - sending him a warmly "High five"!
// ##  - Original Project: https://bitluni.net/am-radio-transmitter | Github: https://github.com/bitluni/ESP32AMRadioTransmitter  | Video: https://youtu.be/lRXHd3HNzEo
// ##  - "AM Radio Transmitter" just runs on legacy Arduino-ESP32 <= v2.x (based on ESP-IDF <= 4.4)
// ##  - Completely re-coded and speed-optimized to work with modern ESP32-API by PPete https://www.ppete.de/
// ##  - "AM Radio Transmitter v2" runs on Arduino-ESP >= v3.x and ESP-IDF >= v5.1
// ##  - Added Multi-frequency selection
// ################################################################################################################################# 

#include <driver/dac_continuous.h>
#include <soc/i2s_reg.h>
#include "blue_danube.h"

#define FREQ 3   // Choose Frequency: 0= 549 kHz | 1= 630 kHz | 2= 729 kHz | 3= 873 kHz | 4= 1107 kHz | 5= 1431 kHz

// necessary to fill "COSINE_SAMPLES10" with a number instead of variable before compilation, otherwise significant delay/signal-interruptions within inner-FOR-loop within loop()
#if FREQ == 0
  #define COSINE_SAMPLES10 160
#elif FREQ == 1
  #define COSINE_SAMPLES10 140
#elif FREQ == 2
  #define COSINE_SAMPLES10 120
#elif FREQ == 3
  #define COSINE_SAMPLES10 100
#elif FREQ == 4
  #define COSINE_SAMPLES10 80
#elif FREQ == 5
  #define COSINE_SAMPLES10 60
#else
  #error "Wrong FREQ!"
#endif

uint8_t buffer[4080];
size_t buffersize_values[6] = {4000, 3920, 4080, 4000, 4080, 4080}; // Maximal sample rate without signal-interruptions using this buffer size together with ".desc_num = 8". Must be type "size_t", otherwise warning occurs
uint8_t cosine_values[6] = {16,14,12,10,8,6};
int8_t cosinetable[16];
uint8_t multTable[256][160];
dac_continuous_handle_t dac_handle;

void generateCosineWave() {
  for (int i = 0; i < (COSINE_SAMPLES10/10); i++) {
    float angle = 2*PI * i / (COSINE_SAMPLES10/10); 
    cosinetable[i] = 127 * cos(angle);  // => values from -127 to 127
  }
}

void swapPairs(int8_t arr[], int8_t len) {
  for (int i = 0; i < len - 1; i += 2) {
    int8_t temp = arr[i];
    arr[i] = arr[i + 1];
    arr[i + 1] = temp;
  }
}

void buildLookupTable() {
  for (int a = 0; a < 256; a++) {
    for (int b = 0; b < COSINE_SAMPLES10; b++) {
      // Ergebnis wieder auf 0–255 skalieren
      multTable[a][b] = (a * cosinetable[b%cosine_values[FREQ]] + 32385) >> 8;  // schneller als Division durch 255
    }
  }
}

unsigned int loopcounter = 0;
uint8_t audiosample = samples[0];  // preload with 1st Audio-sample
unsigned int audiomax_values[6] = {549100, 629900, 729100, 873000,1107000, 1431000}; // number of cosine-carrierwave samples for 1 audio-sample
unsigned int audiomax = audiomax_values[FREQ]/(10*sampleRate);
unsigned int audiopos = 0;  // number of actual Audio-sample within "samples[]"

void updateAudiocounter() {
  loopcounter++;
  if (loopcounter == audiomax) {  // if all sinus carrier-waves of 1 Audio-sample are created, take the next Audio-sample
    loopcounter = 0;
    audiopos++;
    if (audiopos == sampleCount) {  // if end of Audiofile "samples[]" is reached, start from its beginning again
      audiopos = 0;
    }
    audiosample = samples[audiopos] + 128;
  }
}

void setup() {
  generateCosineWave();  // Generation of cosine-carrierwave consisting of 6-16 Samples wavecycle
  swapPairs(cosinetable, cosine_values[FREQ]);  // necessary because ESP32-DAC-API is working with 2-byte little-endian byteorder 
  buildLookupTable();    // used to calculate AM modulation. A lookup-table is faster than a calculation within loop()
  dac_continuous_config_t dac_config = {
    .chan_mask = DAC_CHANNEL_MASK_CH0,  // = DAC1 (ESP-Pin 25)
    .desc_num = 8, 
    .buf_size = buffersize_values[FREQ],
    .freq_hz = 123456,  // not relevant because frequency will be defined later by 4 I2S-Registers 
    .offset = 0,
    .clk_src = DAC_DIGI_CLK_SRC_DEFAULT,  // more stable than DAC_DIGI_CLK_SRC_APLL which creates glitches
    .chan_mode = DAC_CHANNEL_MODE_SIMUL,
  };
  dac_continuous_new_channels(&dac_config, &dac_handle);
  dac_continuous_enable(dac_handle);
  // using the following values creates the highest possible stable medium-wave broadcast frequency depending on number of sample number of one carrier-cosine cycle.
  uint8_t A_values[6] = {19,14,7,49,31,57};
  uint8_t B_values[6] = {2,1,1,8,1,18};
  uint8_t NUM_values[6] = {9,9,9,9,9,9};
  uint8_t BCK_values[6] = {1,1,1,1,1,1};
  // the following 4 Registers define the sample rate used by I2S/DMA-DAC. This is a hack to get higher sample rate than with ".freq_hz" within "dac_config" 
  SET_PERI_REG_BITS(I2S_CLKM_CONF_REG(0), I2S_CLKM_DIV_A_V, A_values[FREQ], I2S_CLKM_DIV_A_S);  
  SET_PERI_REG_BITS(I2S_CLKM_CONF_REG(0), I2S_CLKM_DIV_B_V, B_values[FREQ], I2S_CLKM_DIV_B_S);
  SET_PERI_REG_BITS(I2S_CLKM_CONF_REG(0), I2S_CLKM_DIV_NUM_V, NUM_values[FREQ], I2S_CLKM_DIV_NUM_S);  
  SET_PERI_REG_BITS(I2S_SAMPLE_RATE_CONF_REG(0), I2S_TX_BCK_DIV_NUM_V, BCK_values[FREQ], I2S_TX_BCK_DIV_NUM_S); 
}

void loop() {
  for(int i = 0; i < buffersize_values[FREQ]; i += COSINE_SAMPLES10) {  // filling the Buffer
    for(int j = 0; j < COSINE_SAMPLES10; j++) { // creating 10 Cosine cycles
      buffer[i+j] = multTable[audiosample][j];
    }
    updateAudiocounter();
  }
  dac_continuous_write(dac_handle, buffer,  buffersize_values[FREQ], NULL, -1);  // write buffer to DMA-DAC
}