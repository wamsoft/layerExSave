#include "ncbind.hpp"
#include "savepng.hpp"

#include "zlib.h"

#include <cstring>

#if !defined(_WIN32)
// generic(非Windows)ビルドでは windows.h が無いため DWORD が未定義。
// このファイルでは 32bit 符号なし整数の別名としてのみ使用しているので補う。
typedef tjs_uint32 DWORD;
#endif

#define PNGTYPE_RGBA8888 (0x08060000L)

//---------------------------------------------------------------------------
// 圧縮処理用

class PngChunk : public CompressBase {
	int level;
	enum {
		DEFLATE_INSTEP  = (4096),
		DEFLATE_OUTSTEP = (256*1024)
	};
public:
	PngChunk(CompressBase const *ref)
		: CompressBase(ref), level(Z_DEFAULT_COMPRESSION) { init(); }
	PngChunk()
		: CompressBase(),    level(Z_DEFAULT_COMPRESSION) { init(); }

	virtual ~PngChunk() {}
	void init() {
		resize(4);
		size = cur = 4;
	}
	void setCompressionLevel(int lv) {
		level = lv;
	}
	void writeChunk(CompressBase *target, const char *chunk) {
		writeInt32(*(DWORD*)chunk, 0);
		unsigned long crc = crc32(0, &data[0], size);

		target->writeBigInt32(size-4);
		target->writeBuffer(&data[0], size);
		target->writeBigInt32(crc);
		init();
	}
	void writeUnitType(ncbPropAccessor &dic, const tjs_char *tag, const tjs_char *oneval) {
		writeInt8(dic.getStrValue(tag) == ttstr(oneval) ? 1 : 0);
	}
	inline void writePixel(BufRefT p) {
		resize(cur + 4);
		data[cur++] = p[2];
		data[cur++] = p[1];
		data[cur++] = p[0];
		data[cur++] = p[3];
	}
	static long Deflate(DATA &out,
						unsigned char const * in,
						unsigned long         all,
						int level = Z_DEFAULT_COMPRESSION,
						PngChunk *self = 0)
	{
		z_stream zs;
		memset(&zs, 0, sizeof(zs));
		if (::deflateInit(&zs, level) != Z_OK)
			TVPThrowExceptionMessage(TJS_W("deflate initialize"));

		int s = Z_OK, f = Z_NO_FLUSH;
		bool canceled = false;
		unsigned long rest = all;
		do {
			if (!zs.avail_in) {
				zs.avail_in = DEFLATE_INSTEP;
				zs.next_in  = (Bytef*)in;
				if (zs.avail_in >= rest) {
					zs.avail_in  = rest;
					f = Z_FINISH;
				}
				rest -= zs.avail_in;
				in   += zs.avail_in;
				if (all > 0 && self && self->doProgress((all-rest)*100/all)) {
					canceled = true;
					break;
				}
			}
			if (!zs.avail_out) {
				unsigned long cnt = zs.total_out;
				out.resize(out.size() + DEFLATE_OUTSTEP);
				zs.next_out  = (Bytef*)(&out[0]) + cnt;
				zs.avail_out = out.size()        - cnt;
			}
		} while ((s = ::deflate(&zs, f)) == Z_OK);
		::deflateEnd(&zs);
		if (!canceled) {
			if (self) self->doProgress(100);
			if (s == Z_STREAM_END) return (long)zs.total_out;
			return 0;
		}
		return -1;
	}
	bool deflate() {
		DATA out;
		long cnt = Deflate(out, &data[4], size-4, level, this);
		if (cnt > 0) {
			resize(cnt + 4);
			memcpy((void*)&data[4], &out[0], cnt);
			size = cnt + 4;
		}
		return cnt < 0;
	}
	virtual bool compress(long width, long height, BufRefT buffer, long pitch, iTJSDispatch2 *tagsDict) {
		return false;
	}

	void getCurrentData(const unsigned char* &ptr, size_t &length) const {
		ptr = &data[4];
		length = size-4;
	}
};

/**
 * 画像情報の書き出し
 * @param width 画像横幅
 * @param height 画像縦幅
 * @param buffer 画像バッファ
 * @param pitch 画像データのピッチ
 * @param tagsDict タグ情報
 * @return キャンセルされたら true
 */
bool CompressPNG::compress(long width, long height, BufRefT buffer, long pitch, iTJSDispatch2 *tagsDict)
{
	PngChunk chunk(this);
	/**/   compress_first (chunk, width, height, PNGTYPE_RGBA8888);
	/**/   compress_second(chunk, tagsDict);
	return compress_third (chunk, width, height, buffer, pitch);
}

void CompressPNG::compress_first (PngChunk &chunk, long width, long height, long flag)
{
	// PNG signature
	writeBuffer("\x89PNG\x0D\x0A\x1A\x0A", 8);

	// IHDR chunk
	chunk.writeBigInt32(width );
	chunk.writeBigInt32(height);
	chunk.writeBigInt32(flag  );
	chunk.writeInt8(0); // non interlace
	chunk.writeChunk(this, "IHDR");
}
static const char* ChunkSetResoXY(ncbPropAccessor &dic, PngChunk &chunk, CompressPNG *self=NULL) {
	// pHYs chunk
	if (dic.HasValue(TJS_W("reso_x")) || dic.HasValue(TJS_W("reso_y"))) {
		tjs_int x = dic.getIntValue(TJS_W("reso_x"));
		tjs_int y = dic.getIntValue(TJS_W("reso_y"));
		chunk.writeBigInt32((long)x);
		chunk.writeBigInt32((long)y);
		chunk.writeUnitType(dic, TJS_W("reso_unit"), TJS_W("meter"));
		const char *tag = "pHYs";
		if (self) chunk.writeChunk(self, tag);
		return tag;
	}
	return 0;
}
static const char* ChunkSetOffsXY(ncbPropAccessor &dic, PngChunk &chunk, CompressPNG *self=NULL) {
	// oFFs chunk
	if (dic.HasValue(TJS_W("offs_x")) || dic.HasValue(TJS_W("offs_y"))) {
		tjs_int x = dic.getIntValue(TJS_W("offs_x"));
		tjs_int y = dic.getIntValue(TJS_W("offs_y"));
		chunk.writeBigInt32((long)x);
		chunk.writeBigInt32((long)y);
		chunk.writeUnitType(dic, TJS_W("offs_unit"), TJS_W("micrometer"));
		const char *tag = "oFFs";
		if (self) chunk.writeChunk(self, tag);
		return tag;
	}
	return 0;
}
static const char* ChunkSetVpagWH(ncbPropAccessor &dic, PngChunk &chunk, CompressPNG *self=NULL) {
	// vpAg chunk
	if (dic.HasValue(TJS_W("vpag_w")) || dic.HasValue(TJS_W("vpag_h"))) {
		tjs_int w = dic.getIntValue(TJS_W("vpag_w"));
		tjs_int h = dic.getIntValue(TJS_W("vpag_h"));
		chunk.writeBigInt32((long)w);
		chunk.writeBigInt32((long)h);
		chunk.writeUnitType(dic, TJS_W("vpag_unit"), TJS_W("micrometer"));
		const char *tag = "vpAg";
		if (self) chunk.writeChunk(self, tag);
		return tag;
	}
	return 0;
}
void CompressPNG::compress_second(PngChunk &chunk, iTJSDispatch2 *tagsDict)
{
	// additional chunks
	if (tagsDict == NULL) return;
	ncbPropAccessor dic(tagsDict);

	ChunkSetResoXY(dic, chunk, this);
	ChunkSetOffsXY(dic, chunk, this);
	ChunkSetVpagWH(dic, chunk, this);

	// compression level
	chunk.setCompressionLevel((int)dic.getIntValue(TJS_W("comp_lv"), Z_DEFAULT_COMPRESSION));
}
bool CompressPNG::compress_third (PngChunk &chunk, long width, long height, BufRefT buffer, long pitch)
{
	// IDAT chunk
	for(long y = 0; y < height; y++) {
		BufRefT p = buffer + pitch * y;
		chunk.writeInt8(0);
		for(long x = 0; x < width; x++, p+=4)
			chunk.writePixel(p);
	}
	bool r = chunk.deflate();
	if (!r) {
		chunk.writeChunk(this, "IDAT");
		chunk.writeChunk(this, "IEND");
	}
	return r;
}

static void b64e(tjs_char *p, unsigned char const *r, long len) {
	const tjs_char *b64 = TJS_W("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=");
	long i, stop = len - 3;
	for (i = 0; i <= stop; i+=3) {
		*p++ = b64[ (r[i  ] >> 2) & 0x3F ];
		*p++ = b64[((r[i  ] << 4) & 0x30) | ((r[i+1] >> 4) & 0x0F)];
		*p++ = b64[((r[i+1] << 2) & 0x3C) | ((r[i+2] >> 6) & 0x03)];
		*p++ = b64[ (r[i+2]     ) & 0x3F ];
	}
	switch (len - i) {
	case 2:
		*p++ = b64[ (r[i  ] >> 2) & 0x3F ];
		*p++ = b64[((r[i  ] << 4) & 0x30) | ((r[i+1] >> 4) & 0x0F)];
		*p++ = b64[ (r[i+1] << 2) & 0x3C ];
		*p++ = b64[64];
		*p++ = 0;
		break;
	case 1:
		*p++ = b64[ (r[i  ] >> 2) & 0x3F ];
		*p++ = b64[ (r[i  ] << 4) & 0x30 ];
		*p++ = b64[64];
		*p++ = b64[64];
		*p++ = 0;
		break;
	case 0:
		*p++ = 0;
	}
}

//---------------------------------------------------------------------------
// ダイレクト保存処理
//---------------------------------------------------------------------------
#if defined(LAYEREXSAVE_DISABLE_LODEPNG) && (LAYEREXSAVE_DISABLE_LODEPNG != 0)

#pragma message( ": LodePNG *not* used." )

void CompressPNG::encodeToFile(iTJSDispatch2 *layer, const tjs_char *filename, tTJSVariant *info)
{
	save(layer, filename, info ? info->AsObjectNoAddRef() : NULL);
}

void CompressPNG::encodeToOctet(iTJSDispatch2 *layer, tTJSVariant *vclv, tTJSVariant &ret)
{
	BufRefT buffer;
	long width, height, pitch;

	ret = TJS_W("");
	if (GetLayerBufferAndSize(layer, width, height, buffer, pitch)) {
		PngChunk chunk(this);

		compress_first (chunk, width, height, PNGTYPE_RGBA8888);
		if (vclv && vclv->Type() == tvtObject) {
			compress_second(chunk, vclv->AsObjectNoAddRef());
		} else {
			int comp_lv = vclv ? (int)vclv->AsInteger() : 1;
			chunk.setCompressionLevel(comp_lv);
		}
		compress_third (chunk, width, height, buffer, pitch);

		tTJSVariantOctet *oct = TJSAllocVariantOctet(&data[0], size);
		ret = oct;
		oct->Release();
	}
}

bool CompressPNG::encodeProvinceImage(iTJSDispatch2 *layer, const tjs_char *filename)
{
	// Not Implemented
	return false;
}

#else

#pragma message( ": LodePNG used." )

#include "LodePNG/lodepng.h"
using namespace lexsave; // 名前空間化した lodepng を参照

static bool MakeVectorImage(iTJSDispatch2 *layer, std::vector<unsigned char> &image, long &width, long &height, bool &alpha)
{
	BufRefT buffer;
	long pitch;
	if (!GetLayerBufferAndSize(layer, width, height, buffer, pitch)) return false;

	alpha = false;
	size_t len = width * 4;
	image.resize(len * height);
	for(long y=0, ofs=0; y < height; y++, ofs+=len, buffer+=pitch) {
		BufRefT p = buffer;
		WrtRefT w = &image[ofs];
		for(long x = 0; x < width; x++, p+=4) {
			*w++ = p[2];
			*w++ = p[1];
			*w++ = p[0];
			if ((*w++ = p[3]) != 255) alpha = true;
		}
	}
	return true;
}
static bool MakeVectorProvinceImage(iTJSDispatch2 *layer, std::vector<unsigned char> &image, long &width, long &height)
{
	BufRefT buffer;
	long pitch;
	if (!GetProvinceBufferAndSize(layer, width, height, buffer, pitch)) return false;

	size_t len = width;
	image.resize(len * height);
	for(long y=0, ofs=0; y < height; y++, ofs+=len, buffer+=pitch) {
		BufRefT p = buffer;
		WrtRefT w = &image[ofs];
		for(long x = 0; x < width; x++) *w++ = *p++;
	}
	return true;
}
static void MakeDefaultPalette(WrtRefT table) {
	WrtRefT w = table;
	static unsigned char table2bit[4] = { 0, 128, 192, 255 };
	static unsigned char table3bit[8] = { 0, 64, 96, 128, 160, 192, 224, 255 };
	for (int i = 0; i < 256; i++) {
		// 8bit to 0b GGG RRR BB
		*w++ = table3bit[((i >> 2) & 0x7)];
		*w++ = table3bit[((i >> 5) & 0x7)];
		*w++ = table2bit[((i     ) & 0x3)];
		*w++ = i ? 255 : 0;
	}
}

static unsigned CustomDeflate(unsigned char** out, size_t* outsize,
							  const unsigned char* in, size_t insize,
							  const LodePNGCompressSettings* settings)
{
	int comp_lv = settings ? (int)(tjs_intptr_t)settings->custom_context : 1; //Z_DEFAULT_COMPRESSION;

	std::vector<unsigned char> data;
	long size = PngChunk::Deflate(data, in, insize, comp_lv);
	if (size > 0) {
		*out = (unsigned char*)malloc((size_t)size);
		if (*out) {
			memcpy(*out, &data[0], size);
			*outsize = size;
			return 0;
		} else {
			return 83; /*alloc fail*/
		}
	}
	return 13; /*problem while processing dynamic deflate block*/
}

static void SetInitialState(lodepng::State &state, bool alpha)
{
	state.encoder.auto_convert = 0; // 自動カラー判定処理を無効化
	state.info_png.color.colortype = alpha ? LCT_RGBA : LCT_RGB;
	state.info_png.color.bitdepth  = 8;
	state.info_png.color.key_defined = 0;
}

static bool SetCustomChunk(lodepng::State &state, PngChunk &chunk, const char *tag, int chunkpos = 0)
{
	bool r = false;
	if (tag) {
		const unsigned char *data = 0;
		size_t length = 0;
		chunk.getCurrentData(data, length);
		r = lodepng_chunk_create(&state.info_png.unknown_chunks_data[chunkpos],
								 &state.info_png.unknown_chunks_size[chunkpos],
								 (unsigned)length, tag, data) == 0;
		chunk.init(); // clear buffer
	}
	return r;
}

static bool EncodeLodePNGCommon(std::vector<unsigned char> &image,
								std::vector<unsigned char> &png,
								long width, long height, bool alpha,
								tTJSVariant *info)
{
	lodepng::State state;
	SetInitialState(state, alpha);

	if (info) {
		int comp_lv = -1;
		if (info->Type() == tvtObject) {
			PngChunk chunk;
			ncbPropAccessor dic(info->AsObjectNoAddRef());

			SetCustomChunk(state, chunk, ChunkSetResoXY(dic, chunk));
			SetCustomChunk(state, chunk, ChunkSetOffsXY(dic, chunk));
			SetCustomChunk(state, chunk, ChunkSetVpagWH(dic, chunk));

			// compression level
			if (dic.HasValue(TJS_W("comp_lv"))) {
				comp_lv = (int)dic.getIntValue(TJS_W("comp_lv"), Z_DEFAULT_COMPRESSION);
			}
		} else {
			comp_lv = (int)info->AsInteger();
		}
		if (comp_lv >= 0) {
			state.encoder.zlibsettings.custom_zlib = &CustomDeflate;
			state.encoder.zlibsettings.custom_context = (void*)(tjs_intptr_t)comp_lv;
			if (!comp_lv) state.encoder.filter_strategy = LFS_ZERO;
		}
	}
	return (lodepng::encode(png, image, width, height, state) == 0);
}

void CompressPNG::encodeToFile(iTJSDispatch2 *layer, const tjs_char *filename, tTJSVariant *info)
{
	long width, height;
	bool alpha;

	if (MakeVectorImage(layer, data, width, height, alpha)) {
		DATA png;
		if (EncodeLodePNGCommon(data, png, width, height, alpha, info)) {
			iTJSBinaryStream *out = TVPCreateStream(filename, TJS_BS_WRITE);
			if (!out) {
				TVPThrowExceptionMessage(TJS_W("%1:can't open"), filename);
			}
			try {
				out->Write(&png[0], png.size());
			} catch (...) {
				out->Destruct();
				throw;
			}
			out->Destruct();
		}
	}
}

void CompressPNG::encodeToOctet(iTJSDispatch2 *layer, tTJSVariant *vclv, tTJSVariant &ret)
{
	long width, height;
	bool alpha;

	ret = TJS_W("");
	if (MakeVectorImage(layer, data, width, height, alpha)) {
		DATA png;
		if (EncodeLodePNGCommon(data, png, width, height, alpha, vclv)) {
			tTJSVariantOctet *oct = TJSAllocVariantOctet(&png[0], png.size());
			ret = oct;
			oct->Release();
		}
	}
	// [XXX] reset data
	data.resize(dataSize);
	cur = size = 0;
}

bool CompressPNG::encodeProvinceImage(iTJSDispatch2 *layer, const tjs_char *filename)
{
	long width, height;
	if (MakeVectorProvinceImage(layer, data, width, height)) {
		lodepng::State state;
		state.info_png.color.colortype   = state.info_raw.colortype   = LCT_PALETTE;
		state.info_png.color.bitdepth    = state.info_raw.bitdepth    = 8;
		state.info_png.color.key_defined = state.info_raw.key_defined = 0;
		{
			DATA palette;
			palette.resize(256 * 4);
			MakeDefaultPalette(&palette[0]);
			for (int idx = 0; idx < 256; idx++) {
				unsigned char r = palette[idx*4 + 0],
				/**/          g = palette[idx*4 + 1],
				/**/          b = palette[idx*4 + 2],
				/**/          a = palette[idx*4 + 3];
				lodepng_palette_add(&state.info_png.color, r, g, b, a);
				lodepng_palette_add(&state.info_raw,       r, g, b, a);
			}
		}
		DATA png;
		if (lodepng::encode(png, data, width, height, state) == 0) {
			iTJSBinaryStream *out = TVPCreateStream(filename, TJS_BS_WRITE);
			if (!out) {
				ttstr msg = filename;
				msg += TJS_W(":can't open");
				TVPThrowExceptionMessage(msg.c_str());
			}
			try {
				out->Write(&png[0], png.size());
			} catch (...) {
				out->Destruct();
				throw;
			}
			out->Destruct();
		}
	} else {
		TVPThrowExceptionMessage(TJS_W("no province image"));
	}
	return true;
}
#endif


//---------------------------------------------------------------------------
// レイヤ拡張
//---------------------------------------------------------------------------

/**
 * PNG 形式での画像の保存。注意点:データの保存が終わるまで処理が帰りません。
 * @param filename ファイル名
 * @param tags タグ情報
 */
static tjs_error TJS_INTF_METHOD saveLayerImagePngFunc(tTJSVariant *result,
														tjs_int numparams,
														tTJSVariant **param,
														iTJSDispatch2 *objthis) {
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	CompressPNG png;
	png.encodeToFile(
		objthis, // layer
		param[0]->GetString(),  // filename
		numparams > 1 ? param[1] : NULL // info
		);
	return TJS_S_OK;
}

NCB_ATTACH_FUNCTION(saveLayerImagePng,       Layer, saveLayerImagePngFunc);

/**
 * PNG 形式画像をoctetで返す。注意点:データの保存が終わるまで処理が帰りません。
 * @param compression_level 圧縮率
 */
static tjs_error TJS_INTF_METHOD saveLayerImagePngOctet(tTJSVariant *result,
														tjs_int numparams,
														tTJSVariant **param,
														iTJSDispatch2 *objthis)
{
	if (result) {
		CompressPNG png;
		png.encodeToOctet(objthis, (numparams >= 1) ? param[0] : NULL, *result);
	}
	return TJS_S_OK;
}
NCB_ATTACH_FUNCTION(saveLayerImagePngOctet, Layer, saveLayerImagePngOctet);

/**
 * Province画像を保存(フォーマット：256パレット式PNG固定)
 * @param filename ファイル名
 * @param palette パレット情報（※将来的予約／現在未実装）
 */
static tjs_error TJS_INTF_METHOD saveProvinceImageFunc(tTJSVariant *result,
													   tjs_int numparams,
													   tTJSVariant **param,
													   iTJSDispatch2 *objthis)
{
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	CompressPNG png;
	if (!png.encodeProvinceImage(
		objthis, // layer
		param[0]->GetString()  // filename
		)) return TJS_E_NOTIMPL;
	return TJS_S_OK;
}
NCB_ATTACH_FUNCTION(saveProvinceImage, Layer, saveProvinceImageFunc);

