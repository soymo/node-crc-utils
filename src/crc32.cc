#include <node.h>
#include <node_buffer.h>
#include <v8.h>
#include <stdio.h>
#include <byteswap.h>

#define GF2_DIM 32
#define IS_BIG_ENDIAN (*(uint16_t *)"\0\xff" < 0x100)

unsigned long gf2_matrix_times(unsigned long *mat, unsigned long vec) {
    unsigned long sum = 0;
    while (vec) {
        if (vec & 1) {
            sum ^= *mat;
        }
        vec >>= 1;
        mat++;
    }
    return sum;
}

void gf2_matrix_square(unsigned long *square, unsigned long *mat) {
    int n;
    for (n = 0; n < GF2_DIM; n++) {
        square[n] = gf2_matrix_times(mat, mat[n]);
    }
}

unsigned long crc32_combine(unsigned long crc1, unsigned long crc2, long len2) {
    int n;
    unsigned long row;
    unsigned long even[GF2_DIM];    /* even-power-of-two zeros operator */
    unsigned long odd[GF2_DIM];     /* odd-power-of-two zeros operator */

    /* degenerate case (also disallow negative lengths) */
    if (len2 <= 0) {
        return crc1;
    }

    /* put operator for one zero bit in odd */
    odd[0] = 0xedb88320UL;   /* CRC-32 polynomial */
    row = 1;
    for (n = 1; n < GF2_DIM; n++) {
        odd[n] = row;
        row <<= 1;
    }

    /* put operator for two zero bits in even */
    gf2_matrix_square(even, odd);

    /* put operator for four zero bits in odd */
    gf2_matrix_square(odd, even);

    /* apply len2 zeros to crc1 (first square will put the operator for one
       zero byte, eight zero bits, in even) */
    do {
        /* apply zeros operator for this bit of len2 */
        gf2_matrix_square(even, odd);
        if (len2 & 1) {
            crc1 = gf2_matrix_times(even, crc1);
        }
        len2 >>= 1;

        /* if no more bits set, then done */
        if (len2 == 0) {
            break;
        }
        
        /* another iteration of the loop with odd and even swapped */
        gf2_matrix_square(odd, even);
        if (len2 & 1) {
            crc1 = gf2_matrix_times(odd, crc1);
        }
        len2 >>= 1;

        /* if no more bits set, then done */
    } while (len2 != 0);

    /* return combined crc */
    crc1 ^= crc2;
    return crc1;
}



using namespace v8;
using namespace node;

Handle<Value> crc32_combine(const Arguments& args) {
  HandleScope scope;
  
  if (args.Length() < 3) {
      ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
      return scope.Close(Undefined());
  }

  if (!args[0]->IsNumber() || !args[1]->IsNumber() || !args[2]->IsNumber()) {
      ThrowException(Exception::TypeError(String::New("Wrong arguments")));
      return scope.Close(Undefined());
  }
  
  unsigned long combine = crc32_combine(
    args[0]->NumberValue(), // crc32 #1
    args[1]->NumberValue(), // crc32 #2
    args[2]->NumberValue()  // len2
  );

  int length = sizeof(unsigned long);
  node::Buffer *slowBuffer = node::Buffer::New((char *)&combine, length);
  Local<Object> globalObj = Context::GetCurrent()->Global();
  Local<Function> bufferConstructor = Local<Function>::Cast(globalObj->Get(String::New("Buffer")));
  Handle<Value> constructorArgs[3] = { slowBuffer->handle_, v8::Integer::New(length), v8::Integer::New(0) };
  Local<Object> actualBuffer = bufferConstructor->NewInstance(3, constructorArgs);
  return scope.Close(actualBuffer);
}

bool get_buffer_meta (Handle<Object> buf, unsigned long *crc, unsigned long *len) {
    HandleScope scope;

    if (!Buffer::HasInstance(buf)) {
        return false;
    }

    Local<Object> buffer = buf->ToObject();
    char *data = Buffer::Data(buffer);
    size_t length = Buffer::Length(buffer);

    if (IS_BIG_ENDIAN) {
        *crc = bswap_32((uint32_t) *(data + (length - 8)));
        *len = bswap_32((uint32_t) *(data + (length - 4)));
    } else {
        *crc = (uint32_t) *(data + (length - 8));
        *len = (uint32_t) *(data + (length - 4));
    }

    return true;
}

Handle<Value> crc32_combine_multi(const Arguments& args) {
  HandleScope scope;
  
  if (args.Length() < 1) {
      ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
      return scope.Close(Undefined());
  }

  if (!args[0]->IsArray()) {
      ThrowException(Exception::TypeError(String::New("Wrong arguments")));
      return scope.Close(Undefined());
  }

  Local<Array> arr = Local<Array>::Cast(args[0]);
  uint32_t arLength = arr->Length();
  unsigned long retCrc;
  unsigned long retLen;

  bool meta = get_buffer_meta(Local<Object>::Cast(arr->Get(0)), &retCrc, &retLen);

  if (!meta) {
    return scope.Close(Undefined());
  }

  if (arLength > 1) {
    for (int n=1; n<arLength; n++) {
      unsigned long crc1;
      unsigned long len2;

      meta = get_buffer_meta (Local<Object>::Cast(arr->Get(n)), &crc1, &len2);
      if (!meta) {
        return scope.Close(Undefined());
      }

      retCrc = crc32_combine(retCrc, crc1, len2);
      retLen += len2;
    }
  }

  int length = sizeof(unsigned long);
  node::Buffer *slowBuffer = node::Buffer::New((char *)&retCrc, length);
  Local<Object> globalObj = Context::GetCurrent()->Global();
  Local<Function> bufferConstructor = Local<Function>::Cast(globalObj->Get(String::New("Buffer")));
  Handle<Value> constructorArgs[3] = { slowBuffer->handle_, v8::Integer::New(length), v8::Integer::New(0) };
  Local<Object> actualBuffer = bufferConstructor->NewInstance(3, constructorArgs);

  node::Buffer *slowBufferLen = node::Buffer::New((char *)&retLen, length);
  Local<Object> globalObjLen = Context::GetCurrent()->Global();
  Local<Function> bufferConstructorLen = Local<Function>::Cast(globalObjLen->Get(String::New("Buffer")));
  Handle<Value> constructorArgsLen[3] = { slowBufferLen->handle_, v8::Integer::New(length), v8::Integer::New(0) };
  Local<Object> actualBufferLen = bufferConstructorLen->NewInstance(3, constructorArgsLen);
  
  Local<Number> numRetLen = Number::New(retLen);
  
  Local<Object> retValObj = Object::New();
  retValObj->Set(String::NewSymbol("combinedCrc32"), actualBuffer);
  retValObj->Set(String::NewSymbol("intLength"), numRetLen);
  retValObj->Set(String::NewSymbol("bufferLength"), actualBufferLen);
  
  return scope.Close(retValObj);
}

void init(Handle<Object> exports) {
  exports->Set(String::NewSymbol("crc32_combine"), FunctionTemplate::New(crc32_combine)->GetFunction());
  exports->Set(String::NewSymbol("crc32_combine_multi"), FunctionTemplate::New(crc32_combine_multi)->GetFunction());
}

NODE_MODULE(crc32, init)

