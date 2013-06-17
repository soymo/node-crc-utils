var crc32 = require('./build/default/crc32');
var compress_buffer_crc32 = require('compress-buffer-crc32');
var binpack = require('binpack');

exports.compress = function (body) {
    var concatBuff = compress_buffer_crc32.compress(body, -1);
    var len = concatBuff.length;

    /*console.log('c', concatBuff, len);
    console.log('d', 10, len - 10);
    console.log('l', len-4, len);
    console.log('s', len-8, len-4);*/

    return {
        data: concatBuff.slice(10,len - 10),
        len: binpack.unpackUInt32(concatBuff.slice(len-4), 'little'),
        crc: binpack.unpackUInt32(concatBuff.slice(len-8, len-4), 'little')
    };
};

exports.crc32_combine_multi = crc32.crc32_combine_multi;
