
// ----------------------------------------------------------------------
// This first part assumes we're using the garbage collector, which
// the evaluator always does.  It's a little complicated out of
// necessity.

// First include the auto-generated header of #defines for the gc
// library configuration.
#if !defined(__osf__)
#include <used_gc_flags.h>
#endif

// Now include gc.h
#include <gc.h>

// ----------------------------------------------------------------------

#include <Basics.H>
#include <Units.H>
#include <OS.H>

using std::ostream;
using std::endl;

static void max_Text_len(/*IN/OUT*/ unsigned int &len, const Text &t)
{
  unsigned int t_len = t.Length();
  if(t_len > len)
    {
      len = t_len;
    }
}

void PrintMemStat(ostream& vout) 
{
  unsigned long total, resident;
  OS::GetProcessSize(total, resident);

  Basics::uint64
    gc_heap_sz = GC_get_heap_size(),
    gc_total_bytes = GC_get_total_bytes(),
    gc_count = GC_gc_no;

  Text
    unit_total_t = Basics::FormatUnitVal(total),
    raw_total_t = Text::printf("(%lu)", total),
    unit_resident_t = Basics::FormatUnitVal(resident),
    raw_resident_t = Text::printf("(%lu)", resident),
    unit_gch_t = Basics::FormatUnitVal(gc_heap_sz),
    raw_gch_t = Text::printf("(%"FORMAT_LENGTH_INT_64"u)", gc_heap_sz),
    unit_gct_t = Basics::FormatUnitVal(gc_total_bytes),
    raw_gct_t = Text::printf("(%"FORMAT_LENGTH_INT_64"u)", gc_total_bytes),
    raw_gcc_t = Text::printf("%"FORMAT_LENGTH_INT_64"u", gc_count);

  unsigned int unit_pad_len = unit_total_t.Length();
  max_Text_len(unit_pad_len, unit_resident_t);
  max_Text_len(unit_pad_len, unit_gch_t);
  max_Text_len(unit_pad_len, unit_gct_t);
  max_Text_len(unit_pad_len, raw_gcc_t);
  unsigned int raw_pad_len = raw_total_t.Length();
  max_Text_len(raw_pad_len, raw_resident_t);
  max_Text_len(raw_pad_len, raw_gch_t);
  max_Text_len(raw_pad_len, raw_gct_t);

  vout << endl
       << "Memory Stats:" << endl
       << "  Total Memory Size        : " << unit_total_t.PadLeft(unit_pad_len) << " " << raw_total_t.PadLeft(raw_pad_len) << endl
       << "  Resident Size            : " << unit_resident_t.PadLeft(unit_pad_len) << " " << raw_resident_t.PadLeft(raw_pad_len) << endl
       << "  GC Heap Size             : " << unit_gch_t.PadLeft(unit_pad_len) << " " << raw_gch_t.PadLeft(raw_pad_len) << endl
       << "  GC Total Allocated Bytes : " << unit_gct_t.PadLeft(unit_pad_len) << " " << raw_gct_t.PadLeft(raw_pad_len) << endl
       << "  Garbage Collection Count : " << raw_gcc_t.PadLeft(unit_pad_len) << endl;
}
