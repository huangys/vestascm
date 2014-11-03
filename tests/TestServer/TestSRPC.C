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

#include <Basics.H>
#include <SRPC.H>
#include <MultiSRPC.H>

using std::endl;
using std::cout;
using std::cerr;

bool debug_output = false;

const Text CLIENT_PROG = "TestClient";
const Text SERVER_PROG = "TestServer";
const Text INTERFACE_1 = "9873";

int ints[] = {17, -12, 43, 95, 0, 301, 768, -903, 1017, 3, 4097};
char *chars[] = {"Hello,", " there", " big", " boy", "."};
// These floats were chosen to be exactly representable in IEEE format.
// This should avoid any rounding differences when testing across platforms.
float floats[] = {42.525, 18.0, -83.5, 31.25};

int port_nums[] = {5678, 9876, 11234};

const int nports = sizeof(port_nums) / sizeof(int);

Text ports[nports];

int minor_counter;

Basics::int16 int16s[10];

Basics::int32 int32s[10];

Basics::int64 int64s[10];

void new_test(const Text &ident)
{
  minor_counter = 0;
  cerr << ident + "...";
}

void ok()
{
  cerr << " ok.\n";
}

void print_connection(SRPC &s)
{
  Text me(s.local_socket());
  cerr << ((me != "") ? me.chars() : "???");
  cerr << "=>";
  Text him(s.remote_socket());
  cerr << ((him != "") ? him.chars() : "???");
}

void fatal(SRPC::failure f)
{
  char buff[20];
  sprintf(buff, "%d", minor_counter);
  cerr << buff;
  sprintf(buff, "%d", f.r);
  cerr << Text(": failure(") + buff + ", " + f.msg + ")\n";
  exit(1);
}

void die(char *m)
{
  SRPC::failure f;
  f.r = 0;
  f.msg = m;
  fatal(f);
}

void wait_for_server()
{
  Basics::thread::pause(2);
}

void init_random()
{
  srandom(time(0));
}

Basics::int16 rand16()
{
  // random returns a 32-bit or larger integer, and thus we just need
  // the lower 16 bits of it.
  Basics::uint32 r1 = random();

  return (Basics::int16) r1 & 0xffff;
}

Basics::int32 rand32()
{
  // random only returns positive 32-bit integers, so we use a second
  // one to randomize the upper bit.
  Basics::uint32 r1 = random();
  Basics::uint32 r2 = random();

  return (Basics::int32) (r1 ^ (r2 << 1));
}

Basics::int32 rand64()
{
  // random only returns positive 32-bit integers, so we use three to
  // make sure we randomize all 64 bits.
  Basics::uint64 r1 = random();
  Basics::uint64 r2 = random();
  Basics::uint64 r3 = random();

  return (Basics::int64) (r1 ^ (r2 << 24) ^ (r3 << 33));
}

void init_arrays()
{
  unsigned int len;

  len = sizeof(int16s) / sizeof(int16s[0]);
  for(unsigned int i = 0; i < len; i++)
    {
      int16s[i] = rand16();
    }

  len = sizeof(int32s) / sizeof(int32s[0]);
  for(unsigned int i = 0; i < len; i++)
    {
      int32s[i] = rand32();
    }

  len = sizeof(int64s) / sizeof(int64s[0]);
  for(unsigned int i = 0; i < len; i++)
    {
      int64s[i] = rand64();
    }
}

// Add2 test

void C_Add2(SRPC &s)
{
  // Add two integers
  new_test("C_Add2");
  s.send_int(ints[0]); minor_counter++;
  s.send_int(ints[1]); minor_counter++;
  s.send_end(); minor_counter++;
  int ires = s.recv_int(); minor_counter++;
  s.recv_end();
  if (ires == ints[0]+ints[1]) ok();
  else die("Callee can't add");
}

void S_Add2(SRPC &s)
{
  // Add two integers
  new_test("S_Add2");
  int i1 = s.recv_int(); minor_counter++;
  int i2 = s.recv_int(); minor_counter++;
  s.recv_end(); minor_counter++;
  s.send_int(i1+i2); minor_counter++;
  s.send_end();
  ok();
}

void C_Add2f(SRPC &s)
{
  // Add two floats
  new_test("C_Add2f");
  s.send_float(floats[0]); minor_counter++;
  if(debug_output) cerr << ">" << floats[0];
  s.send_float(floats[1]); minor_counter++;
  if(debug_output) cerr << ">" << floats[1];
  s.send_end(); minor_counter++;
  float fres = s.recv_float(); minor_counter++;
  if(debug_output) cerr << "<" << fres;
  s.recv_end();
  float fres_expected = floats[0]+floats[1];
  if(debug_output) cerr << "(" << fres_expected << ")";
  if (fres == fres_expected) ok();
  else die("Callee can't add");
}

void S_Add2f(SRPC &s)
{
  // Add two floats
  new_test("S_Add2f");
  float f1 = s.recv_float(); minor_counter++;
  if(debug_output) cerr << "<" << f1;
  float f2 = s.recv_float(); minor_counter++;
  if(debug_output) cerr << "<" << f2;
  s.recv_end(); minor_counter++;
  float fres = f1+f2;
  if(debug_output) cerr << ">" << fres;
  s.send_float(fres); minor_counter++;
  s.send_end();
  ok();
}


// Addn tests

void C_Addna(SRPC &s)
{
  new_test("C_Addna");
  int ints_len = sizeof(ints)/sizeof(int);
  int_seq is;
  int i;
  for (i = 0; i < ints_len; i++) is.append(ints[i]);
  s.send_int_seq(is); minor_counter++;
  s.send_end(); minor_counter++;
  int rsum = s.recv_int(); minor_counter++;
  int sum = 0;
  for (i = 0; i < ints_len; i++) sum += ints[i];
  s.recv_end();
  if (sum == rsum) ok();
  else die("Callee can't sum");
}

void S_Addna(SRPC &s)
{
  new_test("S_Addna");
  int sum = 0, i;
  int_seq is;
  s.recv_int_seq(is); minor_counter++;
  s.recv_end(); minor_counter++;
  for (i = 0; i < is.length(); i++) sum += is[i];
  s.send_int(sum); minor_counter++;
  s.send_end();
  ok();  
}

void C_Addnf(SRPC &s)
{
  new_test("C_Addnf");
  int ints_len = sizeof(ints)/sizeof(int);
  int_seq is((const int *)ints, ints_len);
  s.send_int_seq(is); minor_counter++;
  s.send_end(); minor_counter++;
  int rsum = s.recv_int(); minor_counter++;
  int sum = 0;
  for (int i = 0; i < ints_len; i++) sum += ints[i];
  s.recv_end();
  if (sum == rsum) ok();
  else die("Callee can't sum");
}

void S_Addnf(SRPC &s)
{
  new_test("S_Addnf");
  int sum = 0;
  int buff_bytes = int_seq::min_size(sizeof(ints)/sizeof(int));
  int *buff = NEW_PTRFREE_ARRAY(int, buff_bytes/sizeof(int));
  {
    int_seq is(buff, buff_bytes);
    s.recv_int_seq(is); minor_counter++;
    s.recv_end(); minor_counter++;
    for (int i = 0; i < is.length(); i++) sum += is[i];
    s.send_int(sum); minor_counter++;
    s.send_end();
    ok();
  }
  delete[] buff;
}

void C_Addnfp(SRPC &s)
{
  new_test("C_Addnfp");
  int floats_len = sizeof(floats)/sizeof(float);
  int i;
  s.send_int(floats_len);
  for (i = 0; i < floats_len; i++) {
    if(debug_output) cerr << ">" << floats[i];
    s.send_float(floats[i]);
    minor_counter++;
  }
  s.send_end(); minor_counter++;
  float rsum = s.recv_float(); minor_counter++;
  if(debug_output) cerr << "<" << rsum;
  float sum = 0;
  for (i = 0; i < floats_len; i++) sum += floats[i];
  if(debug_output) cerr << "(" << sum << ")";
  s.recv_end();
  if (sum == rsum) ok();
  else die("Callee can't sum");
}

void S_Addnfp(SRPC &s)
{
  new_test("S_Addnfp");
  float sum = 0;
  int floats_len = s.recv_int();
  for (int i = 0; i < floats_len; i++)
    {
      float fn = s.recv_float();
      if(debug_output) cerr << "<" << fn;
      sum += fn;
      if(debug_output) cerr << "(" << sum << ")";
    }
  s.recv_end(); minor_counter++;
  if(debug_output) cerr << ">" << sum;
  s.send_float(sum); minor_counter++;
  s.send_end();
  ok();  
}

// Cat2 test

void C_Cat2(SRPC &s)
{
  // Concatenate two strings
  new_test("C_Cat2");
  int exp_len = strlen(chars[0]) + strlen(chars[1]) + 1;
  char *cres = NEW_PTRFREE_ARRAY(char, exp_len);
  s.send_chars(chars[0]); minor_counter++;
  s.send_chars(chars[1]); minor_counter++;
  s.send_end(); minor_counter++;
  s.recv_chars_here(cres, exp_len); minor_counter++;
  s.recv_end();
  if (Text(cres) == Text(chars[0])+Text(chars[1])) ok();
  else die("Callee can't cat");
  delete[] cres;
}

void S_Cat2(SRPC &s)
{
  // Concatenate two strings
  new_test("S_Cat2");
  char *s1 = s.recv_chars(); minor_counter++;
  char *s2 = s.recv_chars(); minor_counter++;
  Text t1(s1);
  Text t2(s2);
  delete[] s1;
  delete[] s2;
  s.recv_end(); minor_counter++;
  Text t(t1+t2);
  s.send_chars(t.cchars()); minor_counter++;
  s.send_end();
  ok();
}

// Catn test

void C_Catna(SRPC &s)
{
  new_test("C_Catna");
  int chars_len = sizeof(chars)/sizeof(char*);
  chars_seq cs(3, 10);   // Force an expansion
  int i;
  for (i = 0; i < chars_len; i++) cs.append(chars[i]);
  s.send_chars_seq(cs); minor_counter++;
  s.send_end(); minor_counter++;
  char *rres = s.recv_chars(); minor_counter++;
  Text lres = "";
  for (i = 0; i < chars_len; i++) lres += chars[i];
  s.recv_end();
  if (lres == rres) ok();
  else die("Callee can't cat");
  delete [] rres;
}

void S_Catna(SRPC &s)
{
  new_test("S_Catna");
  chars_seq cs;
  s.recv_chars_seq(cs); minor_counter++;
  s.recv_end(); minor_counter++;
  Text res = "";
  for (int i = 0; i < cs.length(); i++) res += cs[i];
  s.send_chars(res.cchars()); minor_counter++;
  s.send_end();
  ok();  
}

void C_Catnf(SRPC &s)
{
  new_test("C_Catnf");
  int chars_len = sizeof(chars)/sizeof(char*);
  chars_seq cs((const char**)&chars, chars_len);
  s.send_chars_seq(cs); minor_counter++;
  s.send_end(); minor_counter++;
  Text rres; s.recv_Text(/*OUT*/ rres); minor_counter++;
  Text lres = "";
  for (int i = 0; i < chars_len; i++) lres += chars[i];
  s.recv_end();
  if (lres == rres) ok();
  else die("Callee can't cat");
}

void S_Catnf(SRPC &s)
{
  new_test("S_Catnf");
  int sum = 0;
  int chars_len = sizeof(chars)/sizeof(char*);
  int i;
  for (i = 0; i < chars_len; i++) sum += strlen(chars[i])+1;  
  int buff_bytes = chars_seq::min_size(sum);
  char *buff = NEW_PTRFREE_ARRAY(char, buff_bytes);
  {
    chars_seq cs((void**)buff, buff_bytes);
    s.recv_chars_seq(cs); minor_counter++;
    s.recv_end(); minor_counter++;
    Text res = "";
    for (i = 0; i < cs.length(); i++) res += cs[i];
    s.send_Text(res); minor_counter++;
    s.send_end();
    ok(); 
  }
  delete[] buff;
}

// Cat2T test

void C_Cat2T(SRPC &s)
{
  // Concatenate two texts
  new_test("C_Cat2T");
  Text t0 = chars[0];
  Text t1 = chars[1];
  s.send_Text(t0); minor_counter++;
  s.send_Text(t1); minor_counter++;
  s.send_end(); minor_counter++;
  Text tr; s.recv_Text(/*OUT*/ tr); minor_counter++;
  s.recv_end();
  if (tr == t0 + t1) ok();
  else die("Callee can't cat");
}

void S_Cat2T(SRPC &s)
{
  // Concatenate two texts
  new_test("S_Cat2T");
  Text t0; s.recv_Text(/*OUT*/ t0); minor_counter++;
  Text t1; s.recv_Text(/*OUT*/ t1); minor_counter++;
  s.recv_end(); minor_counter++;
  s.send_Text(t0+t1); minor_counter++;
  s.send_end();
  ok();
}

// CatnT test

void C_CatnT(SRPC &s)
{
  // Concatenate n texts
  new_test("C_CatnT");
  int chars_len = sizeof(chars)/sizeof(char*);
  chars_seq cs(3, 10);   // Force an expansion
  int i;
  for (i = 0; i < chars_len; i++) {
    Text t(chars[i]);
    cs.append(t);
  }
  s.send_chars_seq(cs); minor_counter++;
  s.send_end(); minor_counter++;
  char *rres = s.recv_chars(); minor_counter++;
  Text lres = "";
  for (i = 0; i < chars_len; i++) lres += chars[i];
  s.recv_end();
  if (lres == rres) ok();
  else die("Callee can't cat");
  delete [] rres;
}

void S_CatnT(SRPC &s)
{
  // Concatenate n texts
  new_test("S_CatnT");
  chars_seq cs;
  s.recv_chars_seq(cs); minor_counter++;
  s.recv_end(); minor_counter++;
  Text res = "";
  for (int i = 0; i < cs.length(); i++) res += cs[i];
  s.send_chars(res.cchars()); minor_counter++;
  s.send_end();
  ok();  
}

// Seq1 test

void C_Seq1(SRPC &s)
{
  int ints_len = sizeof(ints)/sizeof(int);
  new_test("C_Seq1");
  s.send_seq_start();
  for (int i = 0; i < ints_len; i++) s.send_int(ints[i]);
  s.send_seq_end();
  s.send_end();
  int res = s.recv_int();
  s.recv_end();
  if (res == ints_len) ok();
  else die("Wrong sequence length");
}

void S_Seq1(SRPC &s)
{
  int len = 0;
  new_test("S_Seq1");
  s.recv_seq_start();
  while (true) {
    bool got_end;
    int i = s.recv_int(&got_end);
    if (got_end) break;
    len++;
  }
  s.recv_seq_end();
  s.recv_end();
  s.send_int(len);
  s.send_end();
  ok();
}

// Seq2 test

void C_Seq2(SRPC &s)
{
  int ints_len = sizeof(ints)/sizeof(int);
  int chars_len = sizeof(chars)/sizeof(char*);
  int tot = 0;
  int ncopies = 10;
  new_test("C_Seq2");
  s.send_seq_start();
  s.send_int(ncopies);
  for (; ncopies > 0; ncopies--) {
    int_seq is((const int *)&ints, ints_len);
    chars_seq cs((const char **)&chars, chars_len);
    s.send_int_seq(is);
    tot += is.length();
    s.send_chars_seq(cs);
    tot += cs.length();
  }
  s.send_seq_end();
  s.send_end();
  int res = s.recv_int();
  s.recv_end();
  if (res == tot) ok();
  else die("Wrong received length");
}

void S_Seq2(SRPC &s)
{
  int tot = 0;
  new_test("S_Seq2");
  s.recv_seq_start();
  for (int ncopies = s.recv_int(); ncopies > 0; ncopies--) {
    int_seq is;
    chars_seq cs;
    s.recv_int_seq(is);
    tot += is.length();
    s.recv_chars_seq(cs);
    tot += cs.length();
  }
  s.recv_seq_end();
  s.recv_end();
  s.send_int(tot);
  s.send_end();
  ok();
}

// Add2_64 test

void C_Add2_64(SRPC &s)
{
  // Add two 64-bit integers
  new_test("C_Add2_64");
  s.send_int64(int64s[0]); minor_counter++;
  s.send_int64(int64s[1]); minor_counter++;
  s.send_end(); minor_counter++;
  Basics::int64 ires = s.recv_int64(); minor_counter++;
  s.recv_end();
  if (ires == int64s[0]+int64s[1]) ok();
  else die("Callee can't add");
}

void S_Add2_64(SRPC &s)
{
  // Add two 64-bit integers
  new_test("S_Add2_64");
  Basics::int64 i1 = s.recv_int64(); minor_counter++;
  Basics::int64 i2 = s.recv_int64(); minor_counter++;
  s.recv_end(); minor_counter++;
  s.send_int64(i1+i2); minor_counter++;
  s.send_end();
  ok();
}


// Int16Array

void C_Add_Int16Array(SRPC &s)
{
  Basics::int16 expected = 0;
  int len = sizeof(int16s) / sizeof(int16s[0]);
  for(unsigned int i = 0; i < len; i++)
    {
      expected += int16s[i];
    }
  // Add an array of 16-bit integers
  new_test("C_Add_Int16Array");
  s.send_int16_array(int16s, len); minor_counter++;
  s.send_end(); minor_counter++;
  Basics::int16 result = s.recv_int16(); minor_counter++;
  s.recv_end();
  if (result == expected) ok();
  else die("Callee can't sum");
}

void S_Add_Int16Array(SRPC &s)
{
  // Add an array of 16-bit integers
  new_test("S_Add_Int16Array");
  int len;
  Basics::int16 *array = s.recv_int16_array(len); minor_counter++;
  s.recv_end(); minor_counter++;
  Basics::int16 result = 0;
  for(unsigned int i = 0; i < len; i++)
    {
      result += array[i];
    }
  delete [] array;
  s.send_int16(result); minor_counter++;
  s.send_end();
  ok();
}

// Int32Array

void C_Add_Int32Array(SRPC &s)
{
  Basics::int32 expected = 0;
  int len = sizeof(int32s) / sizeof(int32s[0]);
  for(unsigned int i = 0; i < len; i++)
    {
      expected += int32s[i];
    }
  // Add an array of 32-bit integers
  new_test("C_Add_Int32Array");
  s.send_int32_array(int32s, len); minor_counter++;
  s.send_end(); minor_counter++;
  Basics::int32 result = s.recv_int32(); minor_counter++;
  s.recv_end();
  if (result == expected) ok();
  else die("Callee can't sum");
}

void S_Add_Int32Array(SRPC &s)
{
  // Add an array of 32-bit integers
  new_test("S_Add_Int32Array");
  int len;
  Basics::int32 *array = s.recv_int32_array(len); minor_counter++;
  s.recv_end(); minor_counter++;
  Basics::int32 result = 0;
  for(unsigned int i = 0; i < len; i++)
    {
      result += array[i];
    }
  delete [] array;
  s.send_int32(result); minor_counter++;
  s.send_end();
  ok();
}

// Int64Array

void C_Add_Int64Array(SRPC &s)
{
  Basics::int64 expected = 0;
  int len = sizeof(int64s) / sizeof(int64s[0]);
  for(unsigned int i = 0; i < len; i++)
    {
      expected += int64s[i];
    }
  // Add an array of 64-bit integers
  new_test("C_Add_Int64Array");
  s.send_int64_array(int64s, len); minor_counter++;
  s.send_end(); minor_counter++;
  Basics::int64 result = s.recv_int64(); minor_counter++;
  s.recv_end();
  if (result == expected) ok();
  else die("Callee can't sum");
}

void S_Add_Int64Array(SRPC &s)
{
  // Add an array of 64-bit integers
  new_test("S_Add_Int64Array");
  int len;
  Basics::int64 *array = s.recv_int64_array(len); minor_counter++;
  s.recv_end(); minor_counter++;
  Basics::int64 result = 0;
  for(unsigned int i = 0; i < len; i++)
    {
      result += array[i];
    }
  delete [] array;
  s.send_int64(result); minor_counter++;
  s.send_end();
  ok();
}

// Bools

void C_Bool(SRPC &s)
{
  // Invert twoo booleans
  new_test("C_Bool");
  s.send_bool(true); minor_counter++;
  s.send_bool(false); minor_counter++;
  s.send_end(); minor_counter++;
  bool result1 = s.recv_bool(); minor_counter++;
  bool result2 = s.recv_bool(); minor_counter++;
  s.recv_end();
  if (!result1 && result2) ok();
  else die("Callee can't invert booleans");
}

void S_Bool(SRPC &s)
{
  // Invert twoo booleans
  new_test("S_Bool");
  bool b1 = s.recv_bool(); minor_counter++;
  bool b2 = s.recv_bool(); minor_counter++;
  s.recv_end(); minor_counter++;
  s.send_bool(!b1); minor_counter++;
  s.send_bool(!b2); minor_counter++;
  s.send_end();
  ok();
}

// Fail1 test

void C_Fail1(SRPC &s)
{
  new_test("C_Fail1");
  if (!s.alive())
    die("SRPC::alive incorrectly reports failure");
  s.send_failure(ints[0], chars[0], true);
  if (s.alive())
    die("SRPC::alive fails to report failure");
  ok();
}

void S_Fail1(SRPC &s)
{
  new_test("S_Fail1");
  try {
    int i = s.recv_int();    // shouldn't work
    die("Expected failure wasn't thrown.");
  } catch (SRPC::failure f) {
    if (f.r == ints[0] && f.msg == Text(chars[0])) ok();
    else throw;
  };
}


// Conn2 test

void C_Conn2(const char *server_host)
{
  new_test("C_Conn2");
  SRPC s(SRPC::caller, INTERFACE_1, server_host);
  print_connection(s);

  s.send_end(); minor_counter++;
  s.recv_end();
  ok();
}

void S_Conn2()
{
  new_test("S_Conn2");
  SRPC s(SRPC::callee, INTERFACE_1);
  int proc = SRPC::default_proc_id;
  int intf = SRPC::default_intf_version;

  s.await_call(proc, intf); minor_counter++;
  print_connection(s);
  if (proc != SRPC::default_proc_id) die("Proc id mismatch");
  if (intf != SRPC::default_intf_version) die("Interface version mismatch");
  s.recv_end(); minor_counter++;
  s.send_end();
  ok();
}

// Conn3 test

void C_Conn3(const char *server_host)
{
  new_test("C_Conn3");
  SRPC s(SRPC::caller, INTERFACE_1, server_host);
  print_connection(s);

  s.start_call();
  s.send_end(); minor_counter++;
  s.recv_end();
  ok();
}

void S_Conn3()
{
  new_test("S_Conn3");
  SRPC s(SRPC::callee, INTERFACE_1);
  int proc = SRPC::default_proc_id;
  int intf = SRPC::default_intf_version;

  s.await_call(proc, intf); minor_counter++;
  print_connection(s);
  if (proc != SRPC::default_proc_id) die("Proc id mismatch");
  if (intf != SRPC::default_intf_version) die("Interface version mismatch");
  s.recv_end(); minor_counter++;
  s.send_end();
  ok();
}

// Conn4 test

void C_Conn4(const char *server_host)
{
  new_test("C_Conn4");
  SRPC s(SRPC::caller, INTERFACE_1, server_host);
  print_connection(s);

  s.start_call(ints[0], ints[1]);
  s.send_end(); minor_counter++;
  if (!s.alive()) die("Connection reported dead");
  s.recv_end();
  wait_for_server();  // hack
  if (s.alive()) die("Connection not reported dead");
  ok();
}

void S_Conn4()
{
  new_test("S_Conn4");
  SRPC s(SRPC::callee, INTERFACE_1);
  int proc = SRPC::default_proc_id;
  int intf = SRPC::default_intf_version;

  s.await_call(proc, intf); minor_counter++;
  print_connection(s);
  if (!s.alive()) die("Connection reported dead");
  if (proc != ints[0]) die("Proc id mismatch");
  if (intf != ints[1]) die("Interface version mismatch");
  s.recv_end(); minor_counter++;
  s.send_end();
  ok();
}

// Conn5 test

void C_Conn5(const char *server_host)
{
  new_test("C_Conn5");
  SRPC s(SRPC::caller, INTERFACE_1, server_host);
  print_connection(s);

  s.start_call(ints[0], ints[1]);
  s.send_end(); minor_counter++;
  s.recv_end();
  ok();
}

void S_Conn5()
{
  new_test("S_Conn5");
  SRPC s(SRPC::callee, INTERFACE_1);
  int proc = ints[0];
  int intf = ints[1];

  s.await_call(proc, intf); minor_counter++;
  print_connection(s);
  if (proc != ints[0]) die("Proc id mismatch");
  if (intf != ints[1]) die("Interface version mismatch");
  s.recv_end(); minor_counter++;
  s.send_end();
  ok();
}

// Conn6 test

void C_Conn6(const char *server_host)
{
  new_test("C_Conn6");
  SRPC s(SRPC::caller, INTERFACE_1, server_host);
  print_connection(s);

  try {
    s.start_call(ints[0], ints[1]);
    s.send_end();
    s.recv_end();
    die("Version skew not detected");
  } catch (SRPC::failure f) {
    if (f.r == SRPC::version_skew) ok();
    else throw;
  }
}

void S_Conn6()
{
  new_test("S_Conn6");
  SRPC s(SRPC::callee, INTERFACE_1);
  int proc = ints[0];
  int intf = ints[1]+1;

  try {
    s.await_call(proc, intf);
    print_connection(s);
    s.recv_end();
    s.send_end();
    die("Version skew not detected");
  } catch (SRPC::failure f) {
    if (f.r == SRPC::version_skew) ok();
    else throw;
  }
}

// Conn7 test

void C_Conn7(const char *server_host)
{
  new_test("C_Conn7");
  SRPC s(SRPC::caller, INTERFACE_1, server_host);
  print_connection(s);

  s.start_call();
  s.send_end();
  int dummy = s.recv_int();
  // s.recv_end() is missing, so should send (not throw) failure on destructor
  ok();
}

void S_Conn7()
{
  new_test("S_Conn7");
  SRPC s(SRPC::callee, INTERFACE_1);
  int proc = SRPC::default_proc_id;
  int intf = SRPC::default_intf_version;

  try {
    s.await_call(proc, intf);
    print_connection(s);
    s.recv_end();
    s.send_int(ints[0]);
    s.send_end();
    die("Disappearing partner not detected");
  } catch (SRPC::failure f) {
    if (f.r == SRPC::partner_went_away) ok();
    else throw;
  }
}


// Multi1 test

typedef SRPC *SRPCp;

void C_Multi1(const char *server_host)
{
  new_test("C_Multi1");
  MultiSRPC m;
  int i;

  MultiSRPC::ConnId ids[nports];
  SRPCp sps[nports];

  for (i = 0; i < nports; i++) {
    ids[i] = m.Start(server_host, ports[i], sps[i]); minor_counter++;
    sps[i]->start_call();
    sps[i]->send_end();
    (void)sps[i]->recv_int();
    sps[i]->recv_end();
  }
  for (i = 0; i < nports; i++)
    m.End(ids[i]);
  for (i = 0; i < nports; i++) {
    SRPCp sp;
    MultiSRPC::ConnId id = m.Start(server_host, ports[i], sp); minor_counter++;
    if (id != ids[i]) die("Cache failure");
    sp->send_int(i);
    sp->send_end();
    sp->recv_end();
    m.End(id);
  }
  ok();
}

void S_Multi1()
{
  SRPCp sps[nports];
  new_test("S_Multi1");
  int i;
  for (i = 0; i < nports; i++) {
    sps[i] = NEW_CONSTR(SRPC, (SRPC::callee, ports[i])); 
    minor_counter++;
  }
  for (i = 0; i < nports; i++) {
    int proc = SRPC::default_proc_id;
    int intf = SRPC::default_intf_version;

    sps[i]->await_call(proc, intf);
    sps[i]->recv_end();
    sps[i]->send_int(i);
    sps[i]->send_end();
    minor_counter++;
  }
  for (i = 0; i < nports; i++) {
    (void)sps[i]->recv_int(); minor_counter++;
    sps[i]->recv_end();
    sps[i]->send_end();
    delete sps[i];
  }
  ok();
}


//  Driver program for client side



void be_client(const char *server_host)
{
  init_random();
  init_arrays();
  try {
    {
      new_test("C_Conn1");
      SRPC s(SRPC::caller, INTERFACE_1, server_host);
      print_connection(s);
      ok();

      C_Add2(s);
      C_Add2f(s);
      C_Add2_64(s);
      C_Addna(s);
      C_Addnf(s);
      C_Addnfp(s);
      C_Add_Int16Array(s);
      C_Add_Int32Array(s);
      C_Add_Int64Array(s);

      C_Cat2(s);
      C_Catna(s);
      C_Catnf(s);
      C_Cat2T(s);
      C_CatnT(s);

      C_Seq1(s);
      C_Seq2(s);

      C_Bool(s);

      C_Fail1(s);
    }

    wait_for_server();
    C_Conn2(server_host);
    wait_for_server();
    C_Conn3(server_host);
    wait_for_server();
    C_Conn4(server_host);
    wait_for_server();
    C_Conn5(server_host);
    wait_for_server();
    C_Conn6(server_host);
    wait_for_server();
    C_Conn7(server_host);

    wait_for_server();
    C_Multi1(server_host);
    wait_for_server();

  } catch (SRPC::failure f) { fatal(f); };
}


//  Driver program for server side


void be_server()
{
  try {
    {
      new_test("S_Conn1");
      SRPC s(SRPC::callee, INTERFACE_1);
      ok();

      S_Add2(s);
      S_Add2f(s);
      S_Add2_64(s);
      S_Addna(s);
      S_Addnf(s);
      S_Addnfp(s);
      S_Add_Int16Array(s);
      S_Add_Int32Array(s);
      S_Add_Int64Array(s);

      S_Cat2(s);
      S_Catna(s);
      S_Catnf(s);
      S_Cat2T(s);
      S_CatnT(s);

      S_Seq1(s);
      S_Seq2(s);

      S_Bool(s);

      S_Fail1(s);
    }

    S_Conn2();
    S_Conn3();
    S_Conn4();
    S_Conn5();
    S_Conn6();
    S_Conn7();

    S_Multi1();

  } catch (SRPC::failure f) { fatal(f); };
}

void init() {
  char buff[20];
  for (int i = 0; i < nports; i++) {
    sprintf(buff, "%d", port_nums[i]);
    ports[i] = Text(buff);
  }
}

int main(int argc, char *argv[])
{
  init();
  assert(argc > 0);
  Text prog(argv[0]);
  int arg = 1;
  if((argc > arg) && (strcmp(argv[1], "-debug") == 0))
    {
      debug_output = true;
      arg++;
    }
  const char *host = (argc > arg) ? argv[arg] : "localhost";
  // If there is a slash, start one char past it.  Otherwise, start at
  // 0.
  int name_begin = prog.FindCharR(PathnameSep)+1;
  if (prog.Sub(name_begin, CLIENT_PROG.Length()) == CLIENT_PROG)
    be_client(host);
  else if (prog.Sub(name_begin, SERVER_PROG.Length()) == SERVER_PROG)
    be_server();
  else {
    cerr << "Unrecognized program name: "; cerr << argv[0]; cerr << endl;
    exit(1);
  }

  return 0;
}
