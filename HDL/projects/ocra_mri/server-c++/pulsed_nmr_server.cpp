#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static const double sample_freq_Hz = 122.88e6;

typedef union {
  int32_t le_value;
  unsigned char b[4];
} swappable_int32_t;

typedef struct {
  float gradient_sens_x; // [mT/m/A]
  float gradient_sens_y; // [mT/m/A]
  float gradient_sens_z; // [mT/m/A]
} gradient_spec_t;

typedef struct {
  float gradient_x; // [A]
  float gradient_y; // [A]
  float gradient_z; // [A]
} gradient_offset_t;

typedef enum {
	GRAD_ZERO_DISABLED_OUTPUT = 0,
	GRAD_ZERO_ENABLED_OUTPUT,
	GRAD_OFFSET_ENABLED_OUTPUT
} gradient_state_t;

typedef enum {
	GRAD_AXIS_X = 0,
	GRAD_AXIS_Y,
	GRAD_AXIS_Z
} gradient_axis_t;

/* generate a gradient waveform that just changes a state 

	events like this need a 30us gate time in the sequence
*/
void update_gradient_waveform_state(volatile uint32_t *gx,volatile uint32_t *gy, volatile uint32_t *gz,gradient_state_t state, gradient_offset_t offset)
{
	int32_t vmax_val = (1<<19)-1;
	int32_t vmax_val_1v = vmax_val/10; // Assume a translation of 1A/V
 
	int32_t ramp_accum;
	uint32_t i;
	int32_t ival;
  
	float fLSB = 10.0/((1<<15)-1);
	
	switch(state) {
		default:
		case GRAD_ZERO_DISABLED_OUTPUT:
			// set the DAC register to zero
			gx[0] = 0x001fffff & (0 | 0x00100000);
			gy[0] = 0x001fffff & (0 | 0x00100000);
			gz[0] = 0x001fffff & (0 | 0x00100000);
			// disable the outputs with 2's completment coding
			// 24'b0010 0000 0000 0000 0000 0000;
			gx[1] = 0x00200000;
			gy[1] = 0x00200000;
			gz[1] = 0x00200000;
			break;
		case GRAD_ZERO_ENABLED_OUTPUT:
			gx[0] = 0x001fffff & (0 | 0x00100000);
			gy[0] = 0x001fffff & (0 | 0x00100000);
			gz[0] = 0x001fffff & (0 | 0x00100000);
			// enable the outputs with 2's completment coding
			// 24'b0010 0000 0000 0000 0000 0010;
			gx[1] = 0x00200002;
			gy[1] = 0x00200002;
			gz[1] = 0x00200002;			
			break;
		case GRAD_OFFSET_ENABLED_OUTPUT:
			ival = (int32_t)floor(offset.gradient_x/fLSB)*16;
			gx[0] = 0x001fffff & (ival | 0x00100000);
			ival = (int32_t)floor(offset.gradient_y/fLSB)*16;
			gy[0] = 0x001fffff & (ival | 0x00100000);
			ival = (int32_t)floor(offset.gradient_z/fLSB)*16;
			gz[0] = 0x001fffff & (ival | 0x00100000);
			// enable the outputs with 2's completment coding
			// 24'b0010 0000 0000 0000 0000 0010;
			gx[1] = 0x00200002;
			gy[1] = 0x00200002;
			gz[1] = 0x00200002;	
			break;
	}
	for (int k=2; k<2000; k++) {
		gx[k] = 0x0;
		gy[k] = 0x0;
		gz[k] = 0x0;
	}
}

void clear_gradient_waveforms( volatile uint32_t *gx,volatile uint32_t *gy, volatile uint32_t *gz)
{
	for (int k=0; k<2000; k++) {
		gx[k] = 0x0;
		gy[k] = 0x0;
		gz[k] = 0x0;
	}	
}

// just generate a projection along one dimension
void generate_gradient_waveforms_se_proj(volatile uint32_t *gx,volatile uint32_t *gy, volatile uint32_t *gz, float ROamp, gradient_axis_t axis, gradient_offset_t offset)
{
  printf("Designing a gradient waveform !\n"); fflush(stdout);
  int32_t vmax_val = (1<<19)-1;
  printf("Vmax val 10V = %d\n", vmax_val);
  int32_t vmax_val_1v = vmax_val/10;
  int32_t vmax_val_3v = vmax_val/10;
 
  int32_t ramp_accum;
  uint32_t i;
  int32_t ival;
	
  ramp_accum = 0;
  volatile uint32_t *waveform;
  float offset_val = 0.0;
  
  switch(axis) {
	case GRAD_AXIS_X:
		waveform = gx;
		offset_val = offset.gradient_x;
		break;
	case GRAD_AXIS_Y:
		waveform = gy;
		offset_val = offset.gradient_y;
		break;
	case GRAD_AXIS_Z:
		waveform = gz;
		offset_val = offset.gradient_z;
		break;
  }
  float fLSB = 10.0/((1<<15)-1);
  printf("fLSB = %g Volts\n",fLSB);
  
  // enable the gradients with the prescribed offset current
  ival = (int32_t)floor(offset.gradient_x/fLSB)*16;
  gx[0] = 0x001fffff & (ival | 0x00100000);
  ival = (int32_t)floor(offset.gradient_y/fLSB)*16;
  gy[0] = 0x001fffff & (ival | 0x00100000);
  ival = (int32_t)floor(offset.gradient_z/fLSB)*16;
  gz[0] = 0x001fffff & (ival | 0x00100000);
  
  // enable the outputs with 2's completment coding
  // 24'b0010 0000 0000 0000 0000 0010;
  gx[1] = 0x00200002;
  gy[1] = 0x00200002;
  gz[1] = 0x00200002;
  
  // set the offset current for all 3 axis
  for(int k=2; k<2000; k++)
  {
	  ival = (int32_t)floor(offset.gradient_x/fLSB)*16;
	  gx[k] = 0x001fffff & (ival | 0x00100000);
	  ival = (int32_t)floor(offset.gradient_y/fLSB)*16;
	  gy[k] = 0x001fffff & (ival | 0x00100000);
	  ival = (int32_t)floor(offset.gradient_z/fLSB)*16;
	  gz[k] = 0x001fffff & (ival | 0x00100000);
  }
  
  float fROamplitude = ROamp;
  float fRO_half_moment = 2.8*fROamplitude/2.0; // [ms]*Volts crazy moment
  float fROpreamplitude = fRO_half_moment/0.8;  // Volts
  printf("fROprepamplitude = %f V\n",fROpreamplitude);
  float fROprestep = fROpreamplitude/20.0;
  float fROstep = fROamplitude/20.0;
  float fRO = offset_val;
  
  // Design the X gradient
  // prephaser 200 us rise time, 3V amplitude
  for(i=2; i<22; i++) {
    fRO += fROprestep;
    ival = (int32_t)floor(fRO/fLSB)*16;
    printf("i: %d fRO= %f dec= %d\n",i,fRO,ival);
    waveform[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=22; i<82; i++) {
    ival = (int32_t)floor(fRO/fLSB)*16;
    waveform[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=82; i<102; i++) {
    fRO -= fROprestep;
    ival = (int32_t)floor(fRO/fLSB)*16;
    waveform[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=102; i<122; i++) {
    fRO -= fROstep;
    ival = (int32_t)floor(fRO/fLSB)*16;
    waveform[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=122; i<422; i++) {
    ival = (int32_t)floor(fRO/fLSB)*16;
    waveform[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=422; i<442; i++) {
    fRO += fROstep;
    ival = (int32_t)floor(fRO/fLSB)*16;
    waveform[i] = 0x001fffff & (ival | 0x00100000);
  }
}
/* This function makes gradient waveforms for the original SE sequence (4), with the prephaser immediately before the readout,
   and the phase-encode during the prephaser
   
   this also still includes a state update
   The waveform will play out with a 30us delay
 */
void update_gradient_waveforms_se(volatile uint32_t *gx,volatile uint32_t *gy, volatile uint32_t *gz, float ROamp, float PEamp, gradient_offset_t offset)
{
  printf("Designing a gradient waveform !\n"); fflush(stdout);
  int32_t vmax_val = (1<<19)-1;
  printf("Vmax val 10V = %d\n", vmax_val);
  int32_t vmax_val_1v = vmax_val/10;
  int32_t vmax_val_3v = vmax_val/10;
 
  int32_t ramp_accum;
  uint32_t i;
  int32_t ival;

  ramp_accum = 0;
  
  float fLSB = 10.0/((1<<15)-1);
  printf("fLSB = %g Volts\n",fLSB);
  
  // enable the gradients with the prescribed offset current
  ival = (int32_t)floor(offset.gradient_x/fLSB)*16;
  gx[0] = 0x001fffff & (ival | 0x00100000);
  ival = (int32_t)floor(offset.gradient_y/fLSB)*16;
  gy[0] = 0x001fffff & (ival | 0x00100000);
  ival = (int32_t)floor(offset.gradient_z/fLSB)*16;
  gz[0] = 0x001fffff & (ival | 0x00100000);
  
  // enable the outputs with 2's completment coding
  // 24'b0010 0000 0000 0000 0000 0010;
  gx[1] = 0x00200002;
  gy[1] = 0x00200002;
  gz[1] = 0x00200002;
  
  float fROamplitude = ROamp;
  float fRO_half_moment = 2.8*fROamplitude/2.0; // [ms]*Volts crazy moment
  float fROpreamplitude = fRO_half_moment/0.8;  // Volts
  printf("fROprepamplitude = %f V\n",fROpreamplitude);
  float fROprestep = fROpreamplitude/20.0;
  float fROstep = fROamplitude/20.0;
  float fRO = offset.gradient_x;
  
  // Design the X gradient
  // prephaser 200 us rise time, 3V amplitude
  for(i=2; i<22; i++) {
    fRO += fROprestep;
    ival = (int32_t)floor(fRO/fLSB)*16;
    printf("i: %d fRO= %f dec= %d\n",i,fRO,ival);
    gx[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=22; i<82; i++) {
    ival = (int32_t)floor(fRO/fLSB)*16;
    gx[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=82; i<102; i++) {
    fRO -= fROprestep;
    ival = (int32_t)floor(fRO/fLSB)*16;
    gx[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=102; i<122; i++) {
    fRO -= fROstep;
    ival = (int32_t)floor(fRO/fLSB)*16;
    gx[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=122; i<422; i++) {
    ival = (int32_t)floor(fRO/fLSB)*16;
    gx[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=422; i<442; i++) {
    fRO += fROstep;
    ival = (int32_t)floor(fRO/fLSB)*16;
    gx[i] = 0x001fffff & (ival | 0x00100000);
  }
  
  // Design the Y gradient
  // prephaser 200 us rise time, 3V amplitude
  float fPEamplitude = PEamp;
  float fPEstep = PEamp/20.0;
  float fPE = offset.gradient_y;
  
  for(i=2; i<22; i++) {
    fPE += fPEstep;
    ival = (int32_t)floor(fPE/fLSB)*16;
    gy[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=22; i<82; i++) {
    ival = (int32_t)floor(fPE/fLSB)*16;
    gy[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=82; i<102; i++) {
    fPE -= fPEstep;
    ival = (int32_t)floor(fPE/fLSB)*16;
    gy[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=102; i<442; i++) {
    ival = (int32_t)floor(fPE/fLSB)*16;
    gy[i] = 0x001fffff & (ival | 0x00100000);
  }

  // clear the rest of the buffer
  for(i=442; i<2000; i++) {
    ival = (int32_t)floor(offset.gradient_x/fLSB)*16;
    gx[i] = 0x001fffff & (ival | 0x00100000);
    ival = (int32_t)floor(offset.gradient_y/fLSB)*16;
    gy[i] = 0x001fffff & (ival | 0x00100000);
  }
}

void update_gradient_waveforms_se3d(volatile uint32_t *gx,volatile uint32_t *gy, volatile uint32_t *gz, float ROamp, float PEamp, float PE2amp, gradient_offset_t offset)
{
  printf("Designing a gradient waveform !\n"); fflush(stdout);
  int32_t vmax_val = (1<<19)-1;
  printf("Vmax val 10V = %d\n", vmax_val);
  int32_t vmax_val_1v = vmax_val/10;
  int32_t vmax_val_3v = vmax_val/10;
 
  int32_t ramp_accum;
  uint32_t i;
  int32_t ival;

  ramp_accum = 0;
  
  float fLSB = 10.0/((1<<15)-1);
  printf("fLSB = %g Volts\n",fLSB);
  
  // enable the gradients with the prescribed offset current
  ival = (int32_t)floor(offset.gradient_x/fLSB)*16;
  gx[0] = 0x001fffff & (ival | 0x00100000);
  ival = (int32_t)floor(offset.gradient_y/fLSB)*16;
  gy[0] = 0x001fffff & (ival | 0x00100000);
  ival = (int32_t)floor(offset.gradient_z/fLSB)*16;
  gz[0] = 0x001fffff & (ival | 0x00100000);
  
  // enable the outputs with 2's completment coding
  // 24'b0010 0000 0000 0000 0000 0010;
  gx[1] = 0x00200002;
  gy[1] = 0x00200002;
  gz[1] = 0x00200002;
  
  float fROamplitude = ROamp;
  float fRO_half_moment = 2.8*fROamplitude/2.0; // [ms]*Volts crazy moment
  float fROpreamplitude = fRO_half_moment/0.8;  // Volts
  printf("fROprepamplitude = %f V\n",fROpreamplitude);
  float fROprestep = fROpreamplitude/20.0;
  float fROstep = fROamplitude/20.0;
  float fRO = offset.gradient_x;
  
  // Design the X gradient
  // prephaser 200 us rise time, 3V amplitude
  for(i=2; i<22; i++) {
    fRO += fROprestep;
    ival = (int32_t)floor(fRO/fLSB)*16;
    printf("i: %d fRO= %f dec= %d\n",i,fRO,ival);
    gx[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=22; i<82; i++) {
    ival = (int32_t)floor(fRO/fLSB)*16;
    gx[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=82; i<102; i++) {
    fRO -= fROprestep;
    ival = (int32_t)floor(fRO/fLSB)*16;
    gx[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=102; i<122; i++) {
    fRO -= fROstep;
    ival = (int32_t)floor(fRO/fLSB)*16;
    gx[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=122; i<422; i++) {
    ival = (int32_t)floor(fRO/fLSB)*16;
    gx[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=422; i<442; i++) {
    fRO += fROstep;
    ival = (int32_t)floor(fRO/fLSB)*16;
    gx[i] = 0x001fffff & (ival | 0x00100000);
  }
  
  // Design the Y gradient
  // prephaser 200 us rise time, 3V amplitude
  float fPEamplitude = PEamp;
  float fPEstep = PEamp/20.0;
  float fPE = offset.gradient_y;
  
  for(i=2; i<22; i++) {
    fPE += fPEstep;
    ival = (int32_t)floor(fPE/fLSB)*16;
    gy[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=22; i<82; i++) {
    ival = (int32_t)floor(fPE/fLSB)*16;
    gy[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=82; i<102; i++) {
    fPE -= fPEstep;
    ival = (int32_t)floor(fPE/fLSB)*16;
    gy[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=102; i<442; i++) {
    ival = (int32_t)floor(fPE/fLSB)*16;
    gy[i] = 0x001fffff & (ival | 0x00100000);
  }

  // Design the Z gradient
  // prephaser 200 us rise time, 3V amplitude
  fPEamplitude = PE2amp;
  fPEstep = PE2amp/20.0;
  fPE = offset.gradient_z;
  
  for(i=2; i<22; i++) {
    fPE += fPEstep;
    ival = (int32_t)floor(fPE/fLSB)*16;
    gz[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=22; i<82; i++) {
    ival = (int32_t)floor(fPE/fLSB)*16;
    gz[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=82; i<102; i++) {
    fPE -= fPEstep;
    ival = (int32_t)floor(fPE/fLSB)*16;
    gz[i] = 0x001fffff & (ival | 0x00100000);
  }
  for(i=102; i<442; i++) {
    ival = (int32_t)floor(fPE/fLSB)*16;
    gz[i] = 0x001fffff & (ival | 0x00100000);
  }

  // clear the rest of the buffer
  for(i=442; i<2000; i++) {
    ival = (int32_t)floor(offset.gradient_x/fLSB)*16;
    gx[i] = 0x001fffff & (ival | 0x00100000);
    ival = (int32_t)floor(offset.gradient_y/fLSB)*16;
    gy[i] = 0x001fffff & (ival | 0x00100000);
    ival = (int32_t)floor(offset.gradient_z/fLSB)*16;
    gz[i] = 0x001fffff & (ival | 0x00100000);
  }
}

/*
	This function updates the pulse sequence in the memory with a chosen one through index

*/
void update_pulse_sequence(uint32_t seq_idx, volatile uint32_t *pulseq_memory)
{
 switch(seq_idx) {

  case 0:
    /* 
       setup pulse sequence 0
     
    */   
    // LD64 A[5] -> R[3]
    // A[0]
    pulseq_memory[0]  = 0x00000005;
    pulseq_memory[1]  = 0x10000003;
    
    // LD64 A[6] -> R[4]
    // A[1] 
    pulseq_memory[2]  = 0x00000006;
    pulseq_memory[3]  = 0x10000004;
    
    // PR R[4]
    // A[2]
    // 100 ms wait after putting the pulse
    pulseq_memory[4]  = 0x00D9FB92;
    pulseq_memory[5]  = 0x74000400;
    
    // PR R[3]
    // A[3]
    // 0 second wait after putting the pulse
    pulseq_memory[6]  = 0x00000000;
    pulseq_memory[7]  = 0x74000300;

    // A[4] HALT
    pulseq_memory[8] = 0x00000000;
    pulseq_memory[9] = 0x64000000;
   
    // A[5] ALL LED and ~grad_gate
    pulseq_memory[10]  = 0x00000000;
    pulseq_memory[11]  = 0x00000000;

    // A[6] No LED and turn grad_gate on
    pulseq_memory[12]  = 0x00000004;
    pulseq_memory[13]  = 0x00000000;

    
    /* 
       end pulse sequence 0   
    */ 
    break;
   case 1:
    /* 
       setup pulse sequence 1 
     
    */
    // JUMP vector[0] (reset vector)
  
    // J to address 4 x 8 bytes [32]
    // A[0]
    pulseq_memory[0]  = 0x00000004;
    pulseq_memory[1]  = 0x5C000000;
    
    // A[1]
    pulseq_memory[2]  = 0x00000000;
    pulseq_memory[3]  = 0x00000000;
    
    // A[2]
    pulseq_memory[4]  = 0x00000000;
    pulseq_memory[5]  = 0x00000000;
    
    // A[3]
    pulseq_memory[6]  = 0x00000000;
    pulseq_memory[7]  = 0x00000000;
    
    // @ 8 x 4 bytes [32]: LD64 [88] -> R[4]
    // A[4]
    pulseq_memory[8]  = 0x00000010;
    pulseq_memory[9]  = 0x10000004;
    
    // LD64 [96] -> R[3]
    // A[5]
    pulseq_memory[10]  = 0x00000011;
    pulseq_memory[11]  = 0x10000003;
    
    // LD64 [104] -> R[2]
    // A[6]
    pulseq_memory[12]  = 0x00000012;
    pulseq_memory[13]  = 0x10000002;
    
    // TXOFFSET 0
    // A[7]
    pulseq_memory[14]  = 0x00000000;
    pulseq_memory[15]  = 0x20000000;

    // PR R[4] and unblank for 28 us (4000 clock cycles at 7 ns)
    // A[8]
    pulseq_memory[16]  = 0x00000FA0;
    pulseq_memory[17]  = 0x74000400;
    
     // PR R[3]
    // A[9]
    pulseq_memory[18]  = 0x07735940;
    pulseq_memory[19]  = 0x74000300;

    // TXOFFSET 0x32 [decimal 50]
    // A[A]
    pulseq_memory[20]  = 0x00000032;
    pulseq_memory[21]  = 0x20000000;

    // PR R[4] and unblank for 28 us (4000 clock cycles at 7 ns)
    // A[B] 
    pulseq_memory[22]  = 0x00000FA0;
    pulseq_memory[23]  = 0x74000400;
    
    // PR R[3]
    // A[C]
    pulseq_memory[24]  = 0x07735940;
    pulseq_memory[25]  = 0x74000300;

    // DEC R[2]
    // A[D]
    pulseq_memory[26]  = 0x00000000;
    pulseq_memory[27]  = 0x04000002;
    
    // JNZ R[2] => `PC=7
    // A[E]
    pulseq_memory[28]  = 0x00000007;
    pulseq_memory[29]  = 0x40000002;
    
    // HALT
    // A[F]
    pulseq_memory[30] = 0x00000000;
    pulseq_memory[31] = 0x64000000;
    
    // data @[72]
    // A[10]
    pulseq_memory[32] = 0x0000ffff; // ALL LEDS and TX pulse
    pulseq_memory[33] = 0x00000000;
    // A[11]
    pulseq_memory[34] = 0x00008100; // OUTER LEDS and TX pulse
    pulseq_memory[35] = 0x00000000;
    // A[12]
    pulseq_memory[36] = 0x00000040; // 64
    pulseq_memory[37] = 0x00000000;
    /* 
       end pulse sequence 1   
    */ 
    break;
   
   case 3:
    /* 
       setup pulse sequence 3 
     
    */
    // JUMP vector[0] (reset vector)
  
    // J to address 4 x 8 bytes [32]
    // A[0]
    pulseq_memory[0]  = 0x00000004;
    pulseq_memory[1]  = 0x5C000000;
    
    // A[1]
    pulseq_memory[2]  = 0x00000000;
    pulseq_memory[3]  = 0x00000000;
    
    // A[2]
    pulseq_memory[4]  = 0x00000000;
    pulseq_memory[5]  = 0x00000000;
    
    // A[3]
    pulseq_memory[6]  = 0x00000000;
    pulseq_memory[7]  = 0x00000000;
    
    // @ 8 x 4 bytes [32]: LD64 [88] -> R[4]
    // A[4]
    pulseq_memory[8]  = 0x00000012;
    pulseq_memory[9]  = 0x10000004;
    
    // LD64 [96] -> R[3]
    // A[5]
    pulseq_memory[10]  = 0x00000013;
    pulseq_memory[11]  = 0x10000003;
    
    // LD64 [104] -> R[2]
    // A[6]
    pulseq_memory[12]  = 0x00000014;
    pulseq_memory[13]  = 0x10000002;
    
    // LD64 [104] -> R[5]
    // A[7]
    pulseq_memory[14]  = 0x00000015;
    pulseq_memory[15]  = 0x10000005;

    // TXOFFSET 0
    // A[8]
    pulseq_memory[16]  = 0x00000000;
    pulseq_memory[17]  = 0x20000000;

    // PR R[4] and unblank for 110 us (15714 clock cycles at 7 ns)
    // A[9]
    pulseq_memory[18]  = 0x00003D62;
    pulseq_memory[19]  = 0x74000400;
    
     // PR R[3] and put 5 ms wait (714285 clock cycles at 7 ns)
    // A[A]
    pulseq_memory[20]  = 0x000AE62D;
    pulseq_memory[21]  = 0x74000300;

    // TXOFFSET 0x64 [decimal 100]
    // A[B]
    pulseq_memory[22]  = 0x00000064;
    pulseq_memory[23]  = 0x20000000;

    // PR R[4] and unblank for 260 us (37142 clock cycles at 7 ns)
    // A[C] 
    pulseq_memory[24]  = 0x00009116;
    pulseq_memory[25]  = 0x74000400;
    
    // PR R[3] and put 4 ms wait (571428 clock cycles at 7 ns)
    // A[D]
    pulseq_memory[26]  = 0x0008B824;
    pulseq_memory[27]  = 0x74000300;

    // PR R[5] and put 2 second wait (285714285 clock cycles at 7 ns)
    // A[E]
    pulseq_memory[28]  = 0x1107A76D;
    pulseq_memory[29]  = 0x74000500;

    // DEC R[2]
    // A[F]
    pulseq_memory[30]  = 0x00000000;
    pulseq_memory[31]  = 0x04000002;
    
    // JNZ R[2] => `PC=7
    // A[10]
    pulseq_memory[32]  = 0x00000007;
    pulseq_memory[33]  = 0x40000002;
    
    // HALT
    // A[11]
    pulseq_memory[34] = 0x00000000;
    pulseq_memory[35] = 0x64000000;
    
    // data @[72]
    // A[12]
    pulseq_memory[36] = 0x0000fffd; // ALL LEDS and TX pulse
    pulseq_memory[37] = 0x00000000;
    // A[13]
    pulseq_memory[38] = 0x00008100; // OUTER LEDS and TX pulse
    pulseq_memory[39] = 0x00000000;
    // A[14]
    pulseq_memory[40] = 0x00000100; // 1 TR
    pulseq_memory[41] = 0x00000000;
    // A[15] RX gate
    pulseq_memory[42] = 0x0000aa02; // 64
    pulseq_memory[43] = 0x00000000;
    /* 
       end pulse sequence 3   
    */ 
    break;
    
  case 4:
    /* 
       setup pulse sequence 4 
     
       Spin echo with gradients, 1 repetition only
    */
    // JUMP vector[0] (reset vector)
  
    // J to address 10 x 8 bytes A[B]
    // A[0]
    pulseq_memory[0]  = 0x0000000B;
    pulseq_memory[1]  = 0x5C000000;
    
    // A[1] CMD1 TX GATE & TX PULSE
    pulseq_memory[2]  = 0x00000013;
    pulseq_memory[3]  = 0x00000000;
    
    // A[2] CMD2 OFF
    pulseq_memory[4]  = 0x00000002;
    pulseq_memory[5]  = 0x00000000;
    
    // A[3] CMD3 GRAD GATE
    pulseq_memory[6]  = 0x00000006;
    pulseq_memory[7]  = 0x00000000;
    
    // A[4] CMD4 RX GATE & RXPULSE & GRADGATE
    pulseq_memory[8]  = 0x00000024;
    pulseq_memory[9]  = 0x00000000;
    
    // A[5] LOOP COUNTER (NO repetitions for now)
    pulseq_memory[10]  = 0x00000001;
    pulseq_memory[11]  = 0x00000000;

    // A[6] CMD5 EVERYTHING OFF, BUT DO NOT RESERT RX FIFO
    pulseq_memory[12]  = 0x00000000;
    pulseq_memory[13]  = 0x00000000;

    // A[7] UNUSED 
    pulseq_memory[14]  = 0x00000000;
    pulseq_memory[15]  = 0x00000000;

    // A[8] UNUSED 
    pulseq_memory[16]  = 0x00000000;
    pulseq_memory[17]  = 0x00000000;
    
    // A[9] UNUSED 
    pulseq_memory[18]  = 0x00000000;
    pulseq_memory[19]  = 0x00000000;

    // A[A] UNUSED
    pulseq_memory[20]  = 0x00000000;
    pulseq_memory[21]  = 0x00000000;
    
    // LD64 [5] -> R[2]: Load Loop counter to R[2]
    // A[B] 
    pulseq_memory[22]  = 0x00000005;
    pulseq_memory[23]  = 0x10000002;
    
    // LD64 [1] -> R[3]: Load CMD1 to R[3]
    // A[C]
    pulseq_memory[24]  = 0x00000001;
    pulseq_memory[25]  = 0x10000003;

    // LD64 [2] -> R[4]: Load CMD2 to R[4]
    // A[D]
    pulseq_memory[26]  = 0x00000002;
    pulseq_memory[27]  = 0x10000004;

    // LD64 [3] -> R[5]: Load CMD3 to R[5]
    // A[E]
    pulseq_memory[28]  = 0x00000003;
    pulseq_memory[29]  = 0x10000005;

    // LD64 [4] -> R[6]: Load CMD4 to R[6]
    // A[F]
    pulseq_memory[30]  = 0x00000004;
    pulseq_memory[31]  = 0x10000006;
  
    // LD64 [6] -> R[7]: Load CMD5 to R[7]
    // A[10]
    pulseq_memory[32]  = 0x00000004;
    pulseq_memory[33]  = 0x10000007;

    // TXOFFSET 0
    // A[11]
    pulseq_memory[34]  = 0x00000000;
    pulseq_memory[35]  = 0x20000000;

	// GRADOFFSET 0
	// A[12]
	pulseq_memory[36]  = 0x00000000;
	pulseq_memory[37]  = 0x24000000;
		
    // PR R[3] and unblank for 110 us (15714 clock cycles at 7 ns)
    // Issue CMD1 
    // A[13]
    pulseq_memory[38]  = 0x00003D62;
    pulseq_memory[39]  = 0x74000300;
    
    // PR R[4] and put 5 ms wait (714285 clock cycles at 7 ns)
    // Issue CMD2
    // A[14]
    pulseq_memory[40]  = 0x000AE62D;
    pulseq_memory[41]  = 0x74000400;

    // TXOFFSET 0x64 [decimal 100]
    // A[15]
    pulseq_memory[42]  = 0x00000064;
    pulseq_memory[43]  = 0x20000000;

    // PR R[3] and unblank for 260 us (37142 clock cycles at 7 ns)
    // Issue CMD1
    // A[16] 
    pulseq_memory[44]  = 0x00009116;
    pulseq_memory[45]  = 0x74000300;
    
    // PR R[4] and put 2.5 ms wait (357142 clock cycles at 7 ns)
    // Issue CMD2
    // A[17]
    //pulseq_memory[44]  = 0x00057316;
    pulseq_memory[46]  = 0x00051F62;
    pulseq_memory[47]  = 0x74000400;

    // PR R[5] and put 1 ms wait (142857 clock cycles at 7 ns)
    // Issue CMD3
    // A[18]
    pulseq_memory[48]  = 0x00022E09;
    pulseq_memory[49]  = 0x74000500;

    // PR R[6] and put 500 ms wait (71428500 clock cycles at 7 ns)
    // Issue CMD4
    // A[19]
    pulseq_memory[50]  = 0x0441E994;
    pulseq_memory[51]  = 0x74000600;

    // PR R[7] and put 0 ms wait 
    // Issue CMD5
    // A[1A]
    pulseq_memory[52]  = 0x00000000;
    pulseq_memory[53]  = 0x74000700;

    // DEC R[2]
    // A[1B]
    pulseq_memory[54]  = 0x00000000;
    pulseq_memory[55]  = 0x04000002;
    
    // JNZ R[2] => `PC=0x11
    // A[1C]
    pulseq_memory[56]  = 0x00000011;
    pulseq_memory[57]  = 0x40000002;
    
    // HALT
    // A[1D]
    pulseq_memory[58] = 0x00000000;
    pulseq_memory[59] = 0x64000000;
    
    /* 
       end pulse sequence 4   
    */ 
    break;

  case 2:
    /* 
       setup pulse sequence 2 
    
       This sequence loads 0x40 to R[2] and then subtracts 6 from it in 6 DEC steps.
       Expected result in R[2] is 0x3A
    */
    // JUMP vector[0] (reset vector)
    
    // J to address 4 x 8 bytes [32]
    // A[0]
    pulseq_memory[0]  = 0x00000004;
    pulseq_memory[1]  = 0x5C000000;
    
    // A[1]
    pulseq_memory[2]  = 0x00000000;
    pulseq_memory[3]  = 0x00000000;
    
    // A[2]
    pulseq_memory[4]  = 0x00000000;
    pulseq_memory[5]  = 0x00000000;
    
    // A[3]
    pulseq_memory[6]  = 0x00000000;
    pulseq_memory[7]  = 0x00000000;
    
    // LD64 [104] -> R[2]
    // A[4]
    pulseq_memory[8]  = 0x0000000E;
    pulseq_memory[9]  = 0x10000002;
    
    // DEC R[2]
    // A[5]
    pulseq_memory[10]  = 0x00000000;
    pulseq_memory[11]  = 0x04000002;
    
    // DEC R[2]
    // A[6]
    pulseq_memory[12]  = 0x00000000;
    pulseq_memory[13]  = 0x04000002;
    
    // DEC R[2]
    // A[7]
    pulseq_memory[14]  = 0x00000000;
    pulseq_memory[15]  = 0x04000002;
  
    // DEC R[2]
    // A[8]
    pulseq_memory[16]  = 0x00000000;
    pulseq_memory[17]  = 0x04000002;
    
    // DEC R[2]
    // A[9]
    pulseq_memory[18]  = 0x00000000;
    pulseq_memory[19]  = 0x04000002;
    
    // DEC R[2]
    // A[A]
    pulseq_memory[20]  = 0x00000000;
    pulseq_memory[21]  = 0x04000002;
    
    // HALT
    // A[B]
    pulseq_memory[22] = 0x00000000;
    pulseq_memory[23] = 0x64000000;
      
    // data @[72]
    // A[C]
    pulseq_memory[24] = 0x0000ffff; // ALL LEDS and TX pulse
    pulseq_memory[25] = 0x00000000;
    // A[D]
    pulseq_memory[26] = 0x00008100; // OUTER LEDS and no TX pulse
    pulseq_memory[27] = 0x00000000;
    // A[E]
    pulseq_memory[28] = 0x00000040; // 64
    pulseq_memory[29] = 0x00000000;
    
    /* 
       end pulse sequence 2 
    */
    break;
	
	// service sequence to set gradient state
	case 100:
		// J to address 10 x 8 bytes A[B]
		// A[0]
		pulseq_memory[0]  = 0x00000003;
		pulseq_memory[1]  = 0x5C000000;
		// A[1] CMD1 GRAD GATE
		pulseq_memory[2]  = 0x00000006;
		pulseq_memory[3]  = 0x00000000;
		// A[2] CMD2 OFF
		pulseq_memory[4]  = 0x00000002;
		pulseq_memory[5]  = 0x00000000;
		// GRADOFFSET 0
		// A[3]
		pulseq_memory[6]  = 0x00000000;
		pulseq_memory[7]  = 0x24000000;
		// LD64 [1] -> R[2]
		// A[4]
		pulseq_memory[8]  = 0x00000001;
		pulseq_memory[9]  = 0x10000002;
		// LD64 [2] -> R[3]
		// A[5]
		pulseq_memory[10]  = 0x00000002;
		pulseq_memory[11]  = 0x10000003;		
		// PR [2] CMD1 with 40 us delay
		// A[6]
		pulseq_memory[12]  = 0x00001652;
		pulseq_memory[13]  = 0x74000200;
		// PR [3] CMD2 with no delay
		// A[7]
		pulseq_memory[14]  = 0x00000000;
		pulseq_memory[15]  = 0x74000300;		
		// HALT
		// A[8]
		pulseq_memory[16] = 0x00000000;
		pulseq_memory[17] = 0x64000000;	
	  break;
  default:
    /* this sequence does nothing but halt immediately */
    // HALT
    // A[0]
    pulseq_memory[0] = 0x00000000;
    pulseq_memory[1] = 0x64000000;
  }	
}

int main(int argc, char *argv[])
{
  int fd, sock_server, sock_client;
  void *cfg, *sts;
  volatile uint32_t *slcr, *rx_freq, *rx_rate, *seq_config, *pulseq_memory, *tx_divider;
  volatile uint16_t *rx_cntr, *tx_size;
  //volatile uint8_t *rx_rst, *tx_rst;
  volatile uint64_t *rx_data; 
  void *tx_data;
  float tx_freq;
  struct sockaddr_in addr;
  uint32_t command, value;
  int16_t pulse[32768];
  uint64_t buffer[8192];
  int i, j, size, yes = 1;
  swappable_int32_t lv,bv;
  volatile uint32_t *gradient_memory_x;
  volatile uint32_t *gradient_memory_y;
  volatile uint32_t *gradient_memory_z;
  
  gradient_offset_t gradient_offset;
  // these offsets are in Ampere
  gradient_offset.gradient_x = 0.170;
  gradient_offset.gradient_y = 0.00;
  gradient_offset.gradient_z = 0.00;
  
  if(argc != 3) 
    {
      fprintf(stderr,"Usage: pulsed-nmr_planB frequency program\n");
      fprintf(stderr," Available programs:\n");
      fprintf(stderr," 0\t Permanently enable gradient DAC\n");
      fprintf(stderr," 1\t Basic spin-echo, 3 seconds TR\n");
      fprintf(stderr," 2\t Orthogonal projections\n");
      return -1;
    }
  
  if((fd = open("/dev/mem", O_RDWR)) < 0)
  {
    perror("open");
    return EXIT_FAILURE;
  }

  slcr = mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0xF8000000);
  cfg = mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x40000000);
  sts = mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x40001000);
  rx_data = mmap(NULL, 16*sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x40010000);
  tx_data = mmap(NULL, 16*sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x40020000);
  // TW new stuff
  pulseq_memory = mmap(NULL, 16*sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x40030000);
  seq_config = mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x40040000);  

  /*
    NOTE: The block RAM can only be addressed with 32 bit transactions, so gradient_memory needs to
          be of type uint32_t. The HDL would have to be changed to an 8-bit interface to support per
	  byte transactions
   */
  gradient_memory_x = mmap(NULL, 2*sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x40002000);
  gradient_memory_y = mmap(NULL, 2*sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x40004000);
  gradient_memory_z = mmap(NULL, 2*sysconf(_SC_PAGESIZE), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x40006000);

  printf("Setup standard memory maps !\n"); fflush(stdout);
 
  //rx_rst = ((uint8_t *)(cfg + 0));
  tx_divider = ((uint32_t *)(cfg + 0));

  rx_freq = ((uint32_t *)(cfg + 4));
  rx_rate = ((uint32_t *)(cfg + 8));
  rx_cntr = ((uint16_t *)(sts + 0));

  //tx_rst = ((uint8_t *)(cfg + 1));
  tx_size = ((uint16_t *)(cfg + 12));

  printf("Setting FPGA clock to 143 MHz !\n"); fflush(stdout);
  /* set FPGA clock to 143 MHz */
  slcr[2] = 0xDF0D;
  slcr[92] = (slcr[92] & ~0x03F03F30) | 0x00100700;  
  printf(".... Done !\n"); fflush(stdout);
  
  printf("Erasing pulse sequence memory !\n"); fflush(stdout);
  for(i=0; i<32; i++)
    pulseq_memory[i] = 0x0;
 
  // HALT the microsequencer
  seq_config[0] = 0x00;
  printf("... Done !\n"); fflush(stdout);
  
  /* set default rx phase increment */
  //*rx_freq = (uint32_t)floor(19000000 / sample_freq_Hz * (1<<30) + 0.5);

  // set the NCO to 17.62 MHz
  //*rx_freq = (uint32_t)floor(17620000 / sample_freq_Hz * (1<<30) + 0.5);
  printf("setting frequency to %.4f MHz\n",atoi(argv[1])/1e6f);
  *rx_freq = (uint32_t)floor(atoi(argv[1]) / sample_freq_Hz * (1<<30) + 0.5);

  // set the NCO to 5 MHz
  //*rx_freq = (uint32_t)floor(5000000 / sample_freq_Hz * (1<<30) + 0.5);

  /* set default rx sample rate */
  *rx_rate = 250;

  /* fill tx buffer with zeros */
  memset(tx_data, 0, 65536);

  /* local oscillator for the excitation pulse */
  tx_freq = 19.0e6;
  for(i = 0; i < 32768; i++)
    {
      pulse[i] = 0;
    }
  
  // offset 0, start with 50 us lead-in
  
  //for(i = 64; i <= 128; i=i+2)
  for(i = 64; i <= 96; i=i+2)
  {
    pulse[i] = 7*2300; //(int16_t)floor(8000.0 * sin(i * 2.0 * M_PI * tx_freq / sample_freq_Hz) + 0.5);
  }

  // offset 100 in 32 bit space, start with 50 us lead-in
  //for(i = 264; i <= 392; i=i+2)
  for(i = 264; i <= 296; i=i+2)
  {
    pulse[i] = 14*2300; //(int16_t)floor(8000.0 * sin(i * 2.0 * M_PI * tx_freq / sample_freq_Hz) + 0.5);
  }

  /*
  for(i = 16; i < 30; i=i+2)
  {
    pulse[i] = (int16_t)(14*1600-(i-14)*1600); //(int16_t)floor(8000.0 * sin(i * 2.0 * M_PI * tx_freq / sample_freq_Hz) + 0.5);
  }
  
  // Make a second RF pulse at offset 50 (in 32 bit space)
  for(i=100; i < 130; i=i+2)
    {
      pulse[i] = 14*2300;
    }
  */

  *tx_divider = 200;

  size = 32768-1;
  *tx_size = size;
  memset(tx_data, 0, 65536);
  memcpy(tx_data, pulse, 2 * size);

  uint32_t seq_idx = atoi(argv[2]);
  
  update_pulse_sequence(seq_idx, pulseq_memory);	
	
  
  // Connect to the client
  if((sock_server = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    perror("socket");
    return EXIT_FAILURE;
  }
  setsockopt(sock_server, SOL_SOCKET, SO_REUSEADDR, (void *)&yes , sizeof(yes));

  /* setup listening address */
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET; 
  addr.sin_addr.s_addr = htonl(INADDR_ANY); 
  addr.sin_port = htons(1001); 

  if(bind(sock_server, (struct sockaddr *)&addr, sizeof(addr)) < 0)
  {
    perror("bind");
    return EXIT_FAILURE;
  }

  // Start listening
  printf("%s \n", "Listening...");
  listen(sock_server, 1024);

  while(1)
	{
		if((sock_client = accept(sock_server, NULL, NULL)) < 0)
		{
			perror("accept");
			return EXIT_FAILURE;
		}
		printf("%s \n", "Accepted client");
		
		if(seq_idx == 1) {
			// Spin echo image, 128 matrix
			update_pulse_sequence(4, pulseq_memory);
			for(int avgs=0; avgs<36; avgs++) {
			// Phase encoding gradient loop
			float pe_step = 2.936/44.53; //[A]
			float pe = -63.0*pe_step;
			float ro = 1.865*2.0;
			update_gradient_waveforms_se(gradient_memory_x,gradient_memory_y,gradient_memory_z, 1.0, pe, gradient_offset);
			for(int reps=0; reps<128; reps++) { 
				printf("TR[%d]: go!!\n",reps);
     
				seq_config[0] = 0x00000007;
				usleep(1000000); // sleep 1 second

				printf("Number of RX samples in FIFO: %d\n",*rx_cntr);
      
				// Transfer the data to the client
				// transfer 10 * 5k = 50k samples
				for(i = 0; i < 10; ++i)
				{
					while(*rx_cntr < 10000) usleep(500);
					for(j = 0; j < 5000; ++j) buffer[j] = *rx_data;
					send(sock_client, buffer, 5000*8, MSG_NOSIGNAL | (i<9?MSG_MORE:0));
				}

				printf("stop !!\n");
				seq_config[0] = 0x00000000;
			
				pe = pe+pe_step;
				update_gradient_waveforms_se(gradient_memory_x,gradient_memory_y,gradient_memory_z, 1.0, pe, gradient_offset);

				usleep(4000000);
			}
			}
		} else if (seq_idx == 2) {
			// three projections only
			
			update_pulse_sequence(4, pulseq_memory);
			for(int reps=0; reps<99; reps++) { 
				switch(reps % 3) {
					case 0:
						generate_gradient_waveforms_se_proj(gradient_memory_x,gradient_memory_y,gradient_memory_z,1.0,GRAD_AXIS_X,gradient_offset);
					break;
					case 1:
						generate_gradient_waveforms_se_proj(gradient_memory_x,gradient_memory_y,gradient_memory_z,1.0,GRAD_AXIS_Y,gradient_offset);
					break;
					case 2:
						generate_gradient_waveforms_se_proj(gradient_memory_x,gradient_memory_y,gradient_memory_z,1.0,GRAD_AXIS_Z,gradient_offset);
					break;
				}
				seq_config[0] = 0x00000007;
				usleep(1000000); // sleep 1 second

				printf("Number of RX samples in FIFO: %d\n",*rx_cntr);
      
				// Transfer the data to the client
				// transfer 10 * 5k = 50k samples
				for(i = 0; i < 10; ++i)
				{
					while(*rx_cntr < 10000) usleep(500);
					for(j = 0; j < 5000; ++j) buffer[j] = *rx_data;
					send(sock_client, buffer, 5000*8, MSG_NOSIGNAL | (i<9?MSG_MORE:0));
				}
				printf("stop !!\n");
				seq_config[0] = 0x00000000;
				usleep(3000000);
			}
		} else if(seq_idx == 3) {
		  // Spin echo image, 64 matrix
		  update_pulse_sequence(4, pulseq_memory);
	
		  // Phase encoding gradient loop
		  
		  float pe_step = 2.936/44.53; //[A]
		  float pe = -31.0*pe_step;
		  float ro = 1.865;
		  update_gradient_waveforms_se(gradient_memory_x,gradient_memory_y,gradient_memory_z, ro , pe, gradient_offset);
		  for(int reps=0; reps<64; reps++) { 
		    printf("TR[%d]: go!!\n",reps);
     
		    seq_config[0] = 0x00000007;
		    usleep(1000000); // sleep 1 second
		    
		    printf("Number of RX samples in FIFO: %d\n",*rx_cntr);
		    
		    // Transfer the data to the client
		    // transfer 10 * 5k = 50k samples
		    for(i = 0; i < 10; ++i)
		      {
			while(*rx_cntr < 10000) usleep(500);
			for(j = 0; j < 5000; ++j) buffer[j] = *rx_data;
			send(sock_client, buffer, 5000*8, MSG_NOSIGNAL | (i<9?MSG_MORE:0));
		      }

		    printf("stop !!\n");
		    seq_config[0] = 0x00000000;
		    
		    pe = pe+pe_step;
		    update_gradient_waveforms_se(gradient_memory_x,gradient_memory_y,gradient_memory_z, ro, pe, gradient_offset);
		    
		    usleep(500000);
		  }
		}

		else if(seq_idx == 4) {
		  // Spin echo image, 64x64 matrix
		  update_pulse_sequence(4, pulseq_memory);
		  float pe_step = 2.349/44.53; //[A]
		  float ro = 1.865; // [A]
		  // 3D encoding loop
		  float pe2 = -31.0*pe_step;
		  for(int parts = 0; parts<64; parts++) {
		    // Phase encoding gradient loop
		    float pe = -31.0*pe_step;
		    update_gradient_waveforms_se3d(gradient_memory_x,gradient_memory_y,gradient_memory_z, ro , pe, pe2, gradient_offset);
		    for(int reps=0; reps<64; reps++) { 
		      printf("TR[%d]: go!!\n",parts*64+reps);
		      
		      seq_config[0] = 0x00000007;
		      usleep(1000000); // sleep 1 second
		      
		      printf("Number of RX samples in FIFO: %d\n",*rx_cntr);
		      
		      // Transfer the data to the client
		      // transfer 10 * 5k = 50k samples
		      for(i = 0; i < 10; ++i)
			{
			  while(*rx_cntr < 10000) usleep(500);
			  for(j = 0; j < 5000; ++j) buffer[j] = *rx_data;
			  send(sock_client, buffer, 5000*8, MSG_NOSIGNAL | (i<9?MSG_MORE:0));
			}
		      
		      printf("stop !!\n");
		      seq_config[0] = 0x00000000;
		      
		      pe = pe+pe_step;
		      update_gradient_waveforms_se3d(gradient_memory_x,gradient_memory_y,gradient_memory_z, ro, pe, pe2, gradient_offset);
		      
		      usleep(2000000);
		    }
		    pe2 = pe2+pe_step;
		  }
		}
		
		else if(seq_idx == 5) {
		  // turn on gradients with offset currents
		  update_gradient_waveform_state(gradient_memory_x,gradient_memory_y,gradient_memory_z,GRAD_OFFSET_ENABLED_OUTPUT,gradient_offset);
		  // the gradient state sequence
		  update_pulse_sequence(100, pulseq_memory);	
		  printf("disabling gradients with service sequence 100\n");
		  seq_config[0] = 0x00000007;
		  usleep(1000000); // sleep 1 second	
		  // stop the FPGA again
		  printf("stop !!\n");
		  seq_config[0] = 0x00000000;

		  // take spin-echoes with offset currents enabled
		  update_pulse_sequence(4, pulseq_memory);
		  float pe_step = 2.349/44.53; //[A]
		  float ro = 1.865; // [A]
		  for(int reps=0; reps<1000; reps++) { 
		    printf("TR[%d]: go!!\n",reps);
		    
		    seq_config[0] = 0x00000007;
		    usleep(1000000); // sleep 1 second
		    
		    printf("Number of RX samples in FIFO: %d\n",*rx_cntr);
		    
		    // Transfer the data to the client
		    // transfer 10 * 5k = 50k samples
		    for(i = 0; i < 10; ++i)
		      {
			while(*rx_cntr < 10000) usleep(500);
			for(j = 0; j < 5000; ++j) buffer[j] = *rx_data;
			send(sock_client, buffer, 5000*8, MSG_NOSIGNAL | (i<9?MSG_MORE:0));
		      }
		    
		    printf("stop !!\n");
		    seq_config[0] = 0x00000000;
		    usleep(2000000);
		  }
		}
		// kill the gradients
		update_gradient_waveform_state(gradient_memory_x,gradient_memory_y,gradient_memory_z,GRAD_ZERO_DISABLED_OUTPUT,gradient_offset);
		// the gradient state sequence
		update_pulse_sequence(100, pulseq_memory);	
		printf("disabling gradients with service sequence 100\n");
		seq_config[0] = 0x00000007;
		usleep(1000000); // sleep 1 second	
		// stop the FPGA again
		printf("stop !!\n");
		seq_config[0] = 0x00000000;
		break;

		//return EXIT_SUCCESS;
    
	} // End while loop

  // Close the socket connection
  close(sock_server);
  return EXIT_SUCCESS;
} // End main


