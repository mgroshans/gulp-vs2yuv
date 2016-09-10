var Readable = require('readable-stream').Readable;
var replaceExt = require('replace-ext');
var through = require('through2');
var File = require('vinyl');
var util = require('util');
var vsjs = require('vsjs');

var YuvFromVapoursynth = function () {
    return Readable.call(this);
}
util.inherits(YuvFromVapoursynth, Readable);

YuvFromVapoursynth.prototype._init = function (script, path, opts) {
    try {
        this.Vapoursynth = vsjs.Vapoursynth(script, path);
    } catch (e) {
        this.emit('error', e);
        return;
    }
    this.info = this.Vapoursynth.getInfo();
    this.y4m = opts.y4m;
    this.currentFrame = 0;
    this.frameBuffer = new Buffer(this.info.frameSize);
    if (this.y4m) {
        // TODO throw exception when we can't add headers and support wider variety of YUV formats
        this.push(new Buffer("YUV4MPEG2 C420 W" + this.info.width + " H" + this.info.height +
                " F" + this.info.fps.numerator + ":" + this.info.fps.denominator + " Ip A0:0\n"));
    }
    this.emit('ready');
};

YuvFromVapoursynth.prototype._read = function () {
    if (this.currentFrame >= this.info.numFrames) {
        this.push(null);
        return;
    }

    this.Vapoursynth.getFrame(this.currentFrame++, this.frameBuffer, this.frameCallback.bind(this));
};

YuvFromVapoursynth.prototype.frameCallback = function (err) {
    if (err) {
        this.emit('error', err);
        return;
    }
    
    if (this.y4m) {
        this.push(Buffer.concat([new Buffer("FRAME\n"), this.frameBuffer], this.info.frameSize + 6))
    } else {
        this.push(this.frameBuffer);
    }
};

module.exports = function (opts) {
    opts = opts || {};
    if (typeof opts.y4m === 'undefined') {
        opts.y4m = true;
    }

    var extension = opts.extension || opts.y4m ? '.y4m' : '.yuv';
    return through.obj(function (file, encoding, callback) {
        if (!file.isBuffer()) {
            return callback(new Error('Script contents must be a buffer'));
        }

        new YuvFromVapoursynth()
            .on('error', function (err) {
                callback(err);
            })
            .on('ready', function () {
                var ret = new File({
                    path: replaceExt(file.path, extension),
                    contents: this
                });

                ret.data = ret.data || {};
                ret.data.video = this.info;

                callback(null, ret);
            })
            ._init(file.contents, file.path, opts);
    });
};
