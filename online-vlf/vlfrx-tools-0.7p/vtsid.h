
struct VTSID_HDR
{
   uint32_t magic;

   uint32_t bt_secs;             // Base timestamp, seconds
   uint32_t bt_nsec;             // Base timestamp, nanosecs

   uint32_t nf;               // Number of data fields

   uint32_t type;             // Signal type

   double interval;

   char vtversion[12];
   char ident[48];
   uint32_t pad;

   double frequency;
   double width;

   double spec_base;          // Base frequency of spectrum
   double spec_step;          // Frequency interval
   int32_t spec_size;         // Number of bins
} __attribute__((packed));

#define VTSID_MAGIC_MONITOR   0x73155179
#define VTSID_MAGIC_SPECTRUM  0x4e2d6bce

struct VTSID_FIELD
{
   uint8_t type;              // Field type, one of the SF_ constants
   uint8_t cha;               // First channel
   uint8_t chb;               // Second channel
   uint8_t pad;
} __attribute__((packed));

#define SIGTYPE_CW       1
#define SIGTYPE_NOISE    2
#define SIGTYPE_MSK      3
#define SIGTYPE_SIGNAL   4

static inline char *sigtype_to_txt( int type)
{
   switch( type)
   {
      case SIGTYPE_CW:      return "cw";
      case SIGTYPE_NOISE:   return "noise";
      case SIGTYPE_MSK:     return "msk";
      case SIGTYPE_SIGNAL:  return "signal";
   }

   return "";
}

#define SF_AMPLITUDE      1   // Amplitude
#define SF_PHASE_CAR_360  2   // Absolute phase, mod 360
#define SF_PHASE_MOD_180  3   // Modulation phase, mod 180
#define SF_PHASE_REL      4   // Relative phase
#define SF_BEARING_180    5   // Bearing mod 180
#define SF_BEARING_360    6   // Bearing mod 360
#define SF_PHASE_CAR_180  7   // Absolute phase, mod 180
#define SF_PHASE_CAR_90   8   // Absolute phase, mod 90
#define SF_PHASE_MOD_90   9   // Modulation phase, mod 90

static inline char *fieldtype_to_txt( int type)
{
   switch( type)
   {
      case SF_AMPLITUDE: return "amplitude";
      case SF_PHASE_CAR_180: return "abs phase 180";
      case SF_PHASE_CAR_360: return "abs phase 360";
      case SF_PHASE_MOD_90: return "mod phase 90";
      case SF_PHASE_MOD_180: return "mod phase 180";
      case SF_PHASE_REL: return "rel phase";
      case SF_BEARING_180: return "bearing 180";
      case SF_BEARING_360: return "bearing 360";
   }

   return "unknown";
}

struct VTSID_DATA
{
   uint32_t ts;      // Timestamp, relative to base timestamp, units of 100uS
   float data[1];    // Start of array of field values
} __attribute__((packed));

#define is_monitor(A) ((A)->hdr.magic == VTSID_MAGIC_MONITOR)
#define is_spectrum(A) ((A)->hdr.magic == VTSID_MAGIC_SPECTRUM)

