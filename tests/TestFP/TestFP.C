// Copyright (C) 2001, Compaq Computer Corporation
// 
// This file is part of Vesta.
// 
// Vesta is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// 
// Vesta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

// Last modified on Fri Apr 22 16:39:25 EDT 2005 by irina.furman@intel.com 
//      modified on Fri Jul 16 17:15:03 EDT 2004 by ken@xorian.net  
//      modified on Wed Sep  6 18:35:49 PDT 2000 by mann  
//      modified on Mon Dec 15 15:15:17 PST 1997 by heydon

#include <stdio.h>
#include "FP.H"

using std::cerr;
using std::cout;
using std::endl;

void PrintFP(const FP::Tag& fp, const char *str) throw ()
{
    cout << fp << " = \"" << str << "\"\n";
    cout.flush();
}

int main()
{
  unsigned int failure_count = 0;
    int i;

    // Check size fields.
    if(PolyVal::ByteCnt != 16)
      {
	cerr << "PolyVal::ByteCnt is wrong (it should be 16, but is "
	     << PolyVal::ByteCnt << ")" << endl;
	failure_count++;
      }
    if(FP::ByteCnt != 16)
      {
	cerr << "FP::ByteCnt is wrong (it should be 16, but is "
	     << FP::ByteCnt << ")" << endl;
	failure_count++;
      }
    if(PolyVal::WordCnt != 2)
      {
	cerr << "PolyVal::WordCnt is wrong (it should be 2, but is "
	     << PolyVal::WordCnt << ")" << endl;
	failure_count++;
      }
    if(FP::WordCnt != 2)
      {
	cerr << "FP::WordCnt is wrong (it should be 2, but is "
	     << FP::WordCnt << ")" << endl;
	failure_count++;
      }

    // initialize the random number generator
    srandom(1);

    // Test empty string
    PrintFP(FP::Tag(""), "");
    cout << "\n";

    // Test known string
    const char *known = "The order is fpr, tag, untag";
    PrintFP(FP::Tag(known), known);
    cout << "\n";

    // Test that all results of fingerprinting a known string are the
    // same, regardless of alignment.
    cout << "Testing the effect of word-alignment skew" << endl << endl;

    const char *long_text = "A string longer than a word and a...",
      *short_text = "shorty";
    FP::Tag long_unskewed(long_text), short_unskewed(short_text);

    cout << "Unskewed tags:" << endl;
    PrintFP(long_unskewed, long_text);
    PrintFP(short_unskewed, short_text);
    cout << endl;

    Word *word_buff = NEW_ARRAY(Word, (strlen(long_text) / sizeof(Word)) + 2);
    for(i = 0; i < sizeof(Word); i++)
      {
	char *skewed = ((char *) word_buff) + i;
	strcpy(skewed, long_text);
	FP::Tag long_skewed(skewed);
	PrintFP(long_skewed, skewed);
	if(long_skewed != long_unskewed)
	  {
	    cerr << "  Skew test failed (long, i = " << i << ")!" << endl;
	    failure_count++;
	  }
	strcpy(skewed, short_text);
	FP::Tag short_skewed(skewed);
	PrintFP(short_skewed, skewed);
	if(short_skewed != short_unskewed)
	  {
	    cerr << "  Skew test failed (short, i = " << i << ")!" << endl;
	    failure_count++;
	  }
      }

    // Test that all results of "FP::Extend(FP(a), b)" are identical so long
    // as "Concat(a, b)" is the same string "s".
    cout << endl
	 << "Testing FP::Extend(FP(a), b) where Concat(a, b) is identical"
	 << endl << endl;
    const char *s = "Hello, World";
    const int sLen = strlen(s);
    FP::Tag s_base_tag(s);
    for (i = 0; i < sLen; i++) {
      FP::Tag s_tag(s, i);
      s_tag.Extend(s+i, sLen - i);
      PrintFP(s_tag, s);
      if(s_tag != s_base_tag)
	{
	  cerr << "  Extend test failed (i = " << i << ")!" << endl;
	  failure_count++;
	}
    }
    cout << "\n";

    // Test random buffer
    FP::Tag u, v;
    int buffer[4000], l1, l2, l3;
    char *p = (char *) buffer;

    for (i=0; i < 4000; i++) {
	buffer[i] = random();
    }
    cout << "Testing random strings\n\n";
    cout << "  Extend(Extend(FP(l1), l2), l3) vs. Extend(FP(l1), l2+l3)\n\n";
    for (i=0; i < 50; i++){
	l1 = random() & 0xfff;
	l2 = random() & 0xfff;
	l3 = random() & 0xfff;
	char buff[5];
	cout << "  l1 = "; sprintf(buff, "%4d", l1); cout << buff;
	cout << ", l2 = "; sprintf(buff, "%4d", l2); cout << buff;
	cout << ", l3 = "; sprintf(buff, "%4d", l3); cout << buff;
	cout << "    ";
        u = FP::Tag(p+l1, l2).Extend(p+(l1+l2), l3);
	v = FP::Tag(p+l1, l2+l3);
	if (u == v) {
	    cout << "Worked!\n";
	} else {
	    cout << "Broke!\n";
	    PrintFP(u, "u");
	    PrintFP(v, "v");
	    break;
	}
    }
    cout << "\n";

    // Test fingerprints of '\000', '\001', and '\002'
    FP::Tag c0("\000", 1), c1("\001", 1), c2("\002", 1);
    PrintFP(c0, "\\000"); PrintFP(c1, "\\001"); PrintFP(c2, "\\002");

    // Test raw fingerprint operations
    RawFP fp;
    const char *init = "abcdefg";
    const char *extend = "hijklmnopqrs";
    FP::Tag t0(init);
    FP::Tag t1(init);
    FP::Tag t2(init);
    t0.Extend(extend);
    t1.Unpermute(/*OUT*/ fp);
    t1.ExtendRaw(/*INOUT*/ fp, extend, 3);
    t1.ExtendRaw(/*INOUT*/ fp, extend + 3);
    t1.Permute(fp);
    t2.Unpermute(/*OUT*/ fp);
    for (const char *ptr = extend; *ptr != '\0'; ptr++)
	t2.ExtendRaw(/*INOUT*/ fp, *ptr);
    t2.Permute(fp);
    if (t0 != t1) {
	cerr << "\nExtendRaw(string) failed!\n";
	failure_count++;
    }
    if (t0 != t2) {
	cerr << "\nExtendRaw(char) failed!\n";
	failure_count++;
    }

    // Test convertin fingerprints to/from bytes.
    FP::Tag from_source(p, sizeof(buffer));
    unsigned char tag_bytes[FP::ByteCnt];
    from_source.ToBytes(tag_bytes);
    FP::Tag from_bytes;
    from_bytes.FromBytes(tag_bytes);

    cout << endl
	 << "from source = " << from_source << endl
	 << "bytes       = { ";
    for(i = 0; i < FP::ByteCnt; i++)
      {
	char buff[5];
	sprintf(buff, "%02x", tag_bytes[i]);
	cout << buff;
	if(i < (FP::ByteCnt - 1))
	  {
	    cout << ", ";
	  }
      }
    cout << " }" << endl
	 << "from bytes  = " << from_bytes << endl << endl;

    if(from_bytes != from_source)
      {
	cerr << endl << "Conversion to/from bytes failed!" << endl << endl;
	failure_count++;
      }

    if(failure_count > 0)
      {
	exit(1);
      }
    else
      {
	cout << "All tests passed!" << endl;
      }

    return 0;
}
