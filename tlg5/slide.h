//---------------------------------------------------------------------------
#ifndef SLIDE_H
#define SLIDE_H
//---------------------------------------------------------------------------
#define SLIDE_N 4096
#define SLIDE_M (18+255)
// 静的リンク時にエンジン本体 (krkrz/common/visual/SaveTLG5.cpp) の
// グローバル SlideCompressor と多重定義衝突するため名前空間で分離する。
namespace lexsave {
class SlideCompressor
{
	// スライド辞書法 圧縮クラス
	struct Chain
	{
		int Prev;
		int Next;
	};

	unsigned char Text[SLIDE_N + SLIDE_M - 1];
	int Map[256*256];
	Chain Chains[SLIDE_N];


	unsigned char Text2[SLIDE_N + SLIDE_M - 1];
	int Map2[256*256];
	Chain Chains2[SLIDE_N];


	int S;
	int S2;

public:
	SlideCompressor();
	virtual ~SlideCompressor();

private:
	int GetMatch(const unsigned char*cur, int curlen, int &pos, int s);
	void AddMap(int p);
	void DeleteMap(int p);

public:
	void Encode(const unsigned char *in, long inlen,
		unsigned char *out, long & outlen);

	void Store();
	void Restore();
};
} // namespace lexsave
//---------------------------------------------------------------------------
#endif
//---------------------------------------------------------------------------



