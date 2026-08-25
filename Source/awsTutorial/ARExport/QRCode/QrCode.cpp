/* 
 * QR Code generator library for Unreal Engine
 * 
 * Adapted from Project Nayuki (MIT License)
 * https://www.nayuki.io/page/qr-code-generator-library
 */

#include "ARExport/QRCode/QrCode.hpp"
#include <climits>
#include <cstring>

namespace qrcodegen {

QrSegment::QrSegment(Mode md, int numCh, const TArray<bool>& dt) :
		mode(md),
		numChars(numCh),
		data(dt) {
}

QrSegment::QrSegment(Mode md, int numCh, TArray<bool>&& dt) :
		mode(md),
		numChars(numCh),
		data(MoveTemp(dt)) {
}

QrSegment::Mode QrSegment::getMode() const {
	return mode;
}

int QrSegment::getNumChars() const {
	return numChars;
}

const TArray<bool>& QrSegment::getData() const {
	return data;
}

QrSegment QrSegment::makeBytes(const TArray<uint8>& data) {
	TArray<bool> bb;
	bb.Reserve(data.Num() * 8);
	for (uint8 b : data) {
		for (int i = 7; i >= 0; i--)
			bb.Add((b >> i) & 1);
	}
	return QrSegment(Mode::BYTE, data.Num(), MoveTemp(bb));
}

QrSegment QrSegment::makeNumeric(const char *digits) {
	size_t len = std::strlen(digits);
	TArray<bool> bb;
	for (size_t i = 0; i < len; ) {
		int n = 0;
		int count = 0;
		for (; count < 3 && i < len; count++, i++) {
			char c = digits[i];
			if (c >= '0' && c <= '9')
			{
				n = n * 10 + (c - '0');
			}
		}
		int bitLen = count * 3 + 1;
		for (int j = bitLen - 1; j >= 0; j--)
			bb.Add((n >> j) & 1);
	}
	return QrSegment(Mode::NUMERIC, static_cast<int>(len), MoveTemp(bb));
}

static const char *ALPHANUMERIC_CHARSET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

QrSegment QrSegment::makeAlphanumeric(const char *text) {
	size_t len = std::strlen(text);
	TArray<bool> bb;
	for (size_t i = 0; i < len; ) {
		int temp = 0;
		int count = 0;
		for (; count < 2 && i < len; count++, i++) {
			const char *p = std::strchr(ALPHANUMERIC_CHARSET, text[i]);
			if (p != nullptr)
			{
				temp = temp * 45 + static_cast<int>(p - ALPHANUMERIC_CHARSET);
			}
		}
		int bitLen = count * 5 + 1;
		for (int j = bitLen - 1; j >= 0; j--)
			bb.Add((temp >> j) & 1);
	}
	return QrSegment(Mode::ALPHANUMERIC, static_cast<int>(len), MoveTemp(bb));
}

TArray<QrSegment> QrSegment::makeSegments(const char *text) {
	TArray<QrSegment> result;
	if (text[0] == '\0')
		return result;
	TArray<uint8> bytes;
	for (size_t i = 0, len = std::strlen(text); i < len; i++)
		bytes.Add(static_cast<uint8>(text[i]));
	result.Add(makeBytes(bytes));
	return result;
}

QrSegment QrSegment::makeEci(long assignVal) {
	TArray<bool> bb;
	if (assignVal < (1 << 7)) {
		for (int i = 7; i >= 0; i--)
			bb.Add((assignVal >> i) & 1);
	} else if (assignVal < (1 << 14)) {
		bb.Add(1);
		bb.Add(0);
		for (int i = 13; i >= 0; i--)
			bb.Add((assignVal >> i) & 1);
	} else if (assignVal < 1000000L) {
		bb.Add(1);
		bb.Add(1);
		bb.Add(0);
		for (int i = 20; i >= 0; i--)
			bb.Add((assignVal >> i) & 1);
	}
	return QrSegment(Mode::ECI, 0, MoveTemp(bb));
}

int QrSegment::getTotalBits(const TArray<QrSegment>& segs, int version) {
	int result = 0;
	for (const QrSegment &seg : segs) {
		int ccbits = 0;
		switch (seg.mode) {
			case Mode::NUMERIC:      ccbits = (version < 10 ? 10 : (version < 27 ? 12 : 14)); break;
			case Mode::ALPHANUMERIC: ccbits = (version < 10 ?  9 : (version < 27 ? 11 : 13)); break;
			case Mode::BYTE:         ccbits = (version < 10 ?  8 : 16); break;
			case Mode::KANJI:        ccbits = (version < 10 ?  8 : (version < 27 ? 10 : 12)); break;
			case Mode::ECI:          ccbits = 0; break;
		}
		if (seg.numChars >= (1 << ccbits))
			return -1;
		result += 4 + ccbits + seg.data.Num();
	}
	return result;
}

QrCode::QrCode()
	: version(0)
	, size(0)
	, errorCorrectionLevel(Ecc::MEDIUM)
	, mask(0)
{
}

QrCode QrCode::encodeText(const char *text, Ecc ecl) {
	TArray<QrSegment> segs = QrSegment::makeSegments(text);
	return encodeSegments(segs, ecl);
}

QrCode QrCode::encodeBinary(const TArray<uint8>& data, Ecc ecl) {
	TArray<QrSegment> segs;
	segs.Add(QrSegment::makeBytes(data));
	return encodeSegments(segs, ecl);
}

QrCode QrCode::encodeSegments(const TArray<QrSegment>& segs, Ecc ecl, int minVersion, int maxVersion, int mask, bool boostEcl) {
	int version;
	int dataUsedBits = -1;
	for (version = minVersion; version <= maxVersion; version++) {
		int dataCapacityBits = getNumDataCodewords(version, ecl) * 8;
		dataUsedBits = QrSegment::getTotalBits(segs, version);
		if (dataUsedBits != -1 && dataUsedBits <= dataCapacityBits)
			break;
	}
	
	if (version > maxVersion)
	{
		return QrCode();
	}
	
	if (boostEcl) {
		const Ecc Ecls[3] = {Ecc::MEDIUM, Ecc::QUARTILE, Ecc::HIGH};
		for (Ecc newEcl : Ecls) {
			if (dataUsedBits <= getNumDataCodewords(version, newEcl) * 8)
				ecl = newEcl;
		}
	}
	
	TArray<bool> bb;
	for (const QrSegment &seg : segs) {
		int modeBits = 0;
		switch (seg.getMode()) {
			case QrSegment::Mode::NUMERIC:      modeBits = 1; break;
			case QrSegment::Mode::ALPHANUMERIC: modeBits = 2; break;
			case QrSegment::Mode::BYTE:         modeBits = 4; break;
			case QrSegment::Mode::KANJI:        modeBits = 8; break;
			case QrSegment::Mode::ECI:          modeBits = 7; break;
		}
		for (int i = 3; i >= 0; i--)
			bb.Add((modeBits >> i) & 1);
		int ccbits = 0;
		switch (seg.getMode()) {
			case QrSegment::Mode::NUMERIC:      ccbits = (version < 10 ? 10 : (version < 27 ? 12 : 14)); break;
			case QrSegment::Mode::ALPHANUMERIC: ccbits = (version < 10 ?  9 : (version < 27 ? 11 : 13)); break;
			case QrSegment::Mode::BYTE:         ccbits = (version < 10 ?  8 : 16); break;
			case QrSegment::Mode::KANJI:        ccbits = (version < 10 ?  8 : (version < 27 ? 10 : 12)); break;
			case QrSegment::Mode::ECI:          ccbits = 0; break;
		}
		for (int i = ccbits - 1; i >= 0; i--)
			bb.Add((seg.getNumChars() >> i) & 1);
		for (bool b : seg.getData())
			bb.Add(b);
	}
	
	int dataCapacityBits = getNumDataCodewords(version, ecl) * 8;
	for (int i = 0; i < 4 && bb.Num() < dataCapacityBits; i++)
		bb.Add(false);
	while (bb.Num() % 8 != 0)
		bb.Add(false);
	for (uint8 padByte = 0xEC; bb.Num() < dataCapacityBits; padByte ^= 0xEC ^ 0x11) {
		for (int i = 7; i >= 0; i--)
			bb.Add((padByte >> i) & 1);
	}
	
	TArray<uint8> dataCodewords;
	dataCodewords.SetNumZeroed(bb.Num() / 8);
	for (int i = 0; i < bb.Num(); i++)
		dataCodewords[i >> 3] |= (bb[i] ? 1 : 0) << (7 - (i & 7));
	
	return QrCode(version, ecl, dataCodewords, mask);
}

QrCode::QrCode(int ver, Ecc ecl, const TArray<uint8>& dataCodewords, int msk) :
		version(ver),
		size(ver * 4 + 17),
		errorCorrectionLevel(ecl),
		mask(msk) {
	
	modules.SetNum(size);
	isFunction.SetNum(size);
	for (int y = 0; y < size; ++y)
	{
		modules[y].SetNumZeroed(size);
		isFunction[y].SetNumZeroed(size);
	}

	drawFunctionPatterns();
	const TArray<uint8> allCodewords = addEccAndInterleave(dataCodewords);
	drawCodewords(allCodewords);
	
	if (msk == -1) {
		long minPenalty = LONG_MAX;
		for (int i = 0; i < 8; i++) {
			applyMask(i);
			drawFormatBits(i);
			long penalty = getPenaltyScore();
			if (penalty < minPenalty) {
				mask = i;
				minPenalty = penalty;
			}
			applyMask(i);
		}
	}
	applyMask(mask);
	drawFormatBits(mask);
}

int QrCode::getVersion() const { return version; }
int QrCode::getSize() const { return size; }
QrCode::Ecc QrCode::getErrorCorrectionLevel() const { return errorCorrectionLevel; }
int QrCode::getMask() const { return mask; }
bool QrCode::getModule(int x, int y) const { return (x >= 0 && x < size && y >= 0 && y < size) && module(x, y, false); }
bool QrCode::module(int x, int y, bool isFunc) const { return (isFunc ? isFunction : modules)[y][x]; }

void QrCode::drawFunctionPatterns() {
	for (int i = 0; i < size; i++) {
		setFunctionModule(6, i, i % 2 == 0);
		setFunctionModule(i, 6, i % 2 == 0);
	}
	drawFinderPattern(0, 0);
	drawFinderPattern(size - 7, 0);
	drawFinderPattern(0, size - 7);
	
	if (version >= 2) {
		int numAlign = version / 7 + 2;
		int step = (version == 32) ? 26 : (version * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
		TArray<int> alignPatPos;
		alignPatPos.SetNumZeroed(numAlign);
		alignPatPos[0] = 6;
		for (int i = numAlign - 1, pos = size - 7; i >= 1; i--, pos -= step)
			alignPatPos[i] = pos;
		for (int i = 0; i < numAlign; i++) {
			for (int j = 0; j < numAlign; j++) {
				if (!((i == 0 && j == 0) || (i == 0 && j == numAlign - 1) || (i == numAlign - 1 && j == 0)))
					drawAlignmentPattern(alignPatPos[i], alignPatPos[j]);
			}
		}
	}
	
	drawFormatBits(0);
	drawVersion();
}

void QrCode::drawFinderPattern(int x, int y) {
	for (int dy = -1; dy <= 7; dy++) {
		for (int dx = -1; dx <= 7; dx++) {
			int dist = FMath::Max(FMath::Abs(dx - 3), FMath::Abs(dy - 3));
			int xx = x + dx, yy = y + dy;
			if (xx >= 0 && xx < size && yy >= 0 && yy < size)
				setFunctionModule(xx, yy, dist == 0 || dist == 2 || dist == 3);
		}
	}
}

void QrCode::drawAlignmentPattern(int x, int y) {
	for (int dy = -2; dy <= 2; dy++) {
		for (int dx = -2; dx <= 2; dx++)
			setFunctionModule(x + dx, y + dy, FMath::Max(FMath::Abs(dx), FMath::Abs(dy)) != 1);
	}
}

void QrCode::setFunctionModule(int x, int y, bool isBlack) {
	modules[y][x] = isBlack;
	isFunction[y][x] = true;
}

void QrCode::drawFormatBits(int msk) {
	int data = 0;
	switch (errorCorrectionLevel) {
		case Ecc::LOW:      data = 1; break;
		case Ecc::MEDIUM:   data = 0; break;
		case Ecc::QUARTILE: data = 3; break;
		case Ecc::HIGH:     data = 2; break;
	}
	data = (data << 3) | msk;
	int rem = data;
	for (int i = 0; i < 10; i++)
		rem = (rem << 1) ^ ((rem >> 9) * 0x537);
	int bits = ((data << 10) | rem) ^ 0x5412;
	
	for (int i = 0; i <= 5; i++)
		setFunctionModule(8, i, getBit(bits, i));
	setFunctionModule(8, 7, getBit(bits, 6));
	setFunctionModule(8, 8, getBit(bits, 7));
	setFunctionModule(7, 8, getBit(bits, 8));
	for (int i = 9; i < 15; i++)
		setFunctionModule(14 - i, 8, getBit(bits, i));
	
	for (int i = 0; i < 8; i++)
		setFunctionModule(size - 1 - i, 8, getBit(bits, i));
	for (int i = 8; i < 15; i++)
		setFunctionModule(8, size - 15 + i, getBit(bits, i));
	setFunctionModule(8, size - 8, true);
}

void QrCode::drawVersion() {
	if (version < 7)
		return;
	int rem = version;
	for (int i = 0; i < 12; i++)
		rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
	long bits = (static_cast<long>(version) << 12) | rem;
	for (int i = 0; i < 18; i++) {
		bool bit = getBit(static_cast<int>(bits), i);
		int a = size - 11 + i % 3, b = i / 3;
		setFunctionModule(a, b, bit);
		setFunctionModule(b, a, bit);
	}
}

static uint8 reedSolomonMultiply(uint8 x, uint8 y) {
	int z = 0;
	for (int i = 7; i >= 0; i--) {
		z = (z << 1) ^ ((z >> 8) * 0x11D);
		z ^= ((y >> i) & 1) * x;
	}
	return static_cast<uint8>(z);
}

static TArray<uint8> reedSolomonComputeDivisor(int degree) {
	TArray<uint8> result;
	result.SetNumZeroed(degree);
	result[degree - 1] = 1;
	uint8 root = 1;
	for (int i = 0; i < degree; i++) {
		for (int j = 0; j < result.Num(); j++) {
			result[j] = reedSolomonMultiply(result[j], root);
			if (j + 1 < result.Num())
				result[j] ^= result[j + 1];
		}
		root = reedSolomonMultiply(root, 0x02);
	}
	return result;
}

TArray<uint8> QrCode::addEccAndInterleave(const TArray<uint8>& data) const {
	int numBlocks = NUM_ERROR_CORRECTION_BLOCKS[static_cast<int>(errorCorrectionLevel)][version];
	int blockEccLen = ECC_CODEWORDS_PER_BLOCK[static_cast<int>(errorCorrectionLevel)][version];
	int rawCodewords = getNumRawDataModules(version) / 8;
	int numShortBlocks = numBlocks - rawCodewords % numBlocks;
	int shortBlockLen = rawCodewords / numBlocks;
	
	TArray<TArray<uint8>> blocks;
	const TArray<uint8> rsDiv = reedSolomonComputeDivisor(blockEccLen);
	for (int i = 0, k = 0; i < numBlocks; i++) {
		int datLen = shortBlockLen - blockEccLen + (i >= numShortBlocks ? 1 : 0);
		TArray<uint8> dat;
		dat.Reserve(datLen + blockEccLen);
		for (int d = 0; d < datLen && (k + d) < data.Num(); ++d)
		{
			dat.Add(data[k + d]);
		}
		k += datLen;

		TArray<uint8> ecc;
		ecc.SetNumZeroed(blockEccLen);
		for (uint8 b : dat) {
			uint8 factor = b ^ ecc[0];
			ecc.RemoveAt(0);
			ecc.Add(0);
			for (int j = 0; j < ecc.Num(); j++)
				ecc[j] ^= reedSolomonMultiply(rsDiv[j], factor);
		}
		dat.Append(ecc);
		blocks.Add(MoveTemp(dat));
	}
	
	TArray<uint8> result;
	for (int i = 0; i < blocks[0].Num(); i++) {
		for (int j = 0; j < blocks.Num(); j++) {
			if (i < blocks[j].Num())
				result.Add(blocks[j][i]);
		}
	}
	return result;
}

void QrCode::drawCodewords(const TArray<uint8>& data) {
	int i = 0;
	for (int right = size - 1; right >= 1; right -= 2) {
		if (right == 6)
			right = 5;
		for (int vert = 0; vert < size; vert++) {
			for (int j = 0; j < 2; j++) {
				int x = right - j;
				bool upward = ((right + 1) & 2) == 0;
				int y = upward ? size - 1 - vert : vert;
				if (!isFunction[y][x] && i < data.Num() * 8) {
					modules[y][x] = getBit(data[i >> 3], 7 - (i & 7));
					i++;
				}
			}
		}
	}
}

void QrCode::applyMask(int msk) {
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			bool invert = false;
			switch (msk) {
				case 0:  invert = (x + y) % 2 == 0;                    break;
				case 1:  invert = y % 2 == 0;                          break;
				case 2:  invert = x % 3 == 0;                          break;
				case 3:  invert = (x + y) % 3 == 0;                    break;
				case 4:  invert = (x / 3 + y / 2) % 2 == 0;            break;
				case 5:  invert = x * y % 2 + x * y % 3 == 0;          break;
				case 6:  invert = (x * y % 2 + x * y % 3) % 2 == 0;    break;
				case 7:  invert = ((x + y) % 2 + x * y % 3) % 2 == 0;  break;
			}
			modules[y][x] = modules[y][x] ^ (invert & !isFunction[y][x]);
		}
	}
}

long QrCode::getPenaltyScore() const {
	long result = 0;
	for (int y = 0; y < size; y++) {
		bool runColor = false;
		int runX = 0;
		for (int x = 0; x < size; x++) {
			if (module(x, y, false) == runColor) {
				runX++;
				if (runX == 5)
					result += 3;
				else if (runX > 5)
					result++;
			} else {
				runColor = module(x, y, false);
				runX = 1;
			}
		}
	}
	for (int x = 0; x < size; x++) {
		bool runColor = false;
		int runY = 0;
		for (int y = 0; y < size; y++) {
			if (module(x, y, false) == runColor) {
				runY++;
				if (runY == 5)
					result += 3;
				else if (runY > 5)
					result++;
			} else {
				runColor = module(x, y, false);
				runY = 1;
			}
		}
	}
	return result;
}

bool QrCode::getBit(int x, int i) {
	return ((x >> i) & 1) != 0;
}

int QrCode::getNumDataCodewords(int ver, Ecc ecl) {
	return getNumRawDataModules(ver) / 8 - ECC_CODEWORDS_PER_BLOCK[static_cast<int>(ecl)][ver] * NUM_ERROR_CORRECTION_BLOCKS[static_cast<int>(ecl)][ver];
}

int QrCode::getNumRawDataModules(int ver) {
	int result = (16 * ver + 128) * ver + 64;
	if (ver >= 2) {
		int numAlign = ver / 7 + 2;
		result -= (25 * numAlign - 10) * numAlign - 55;
		if (ver >= 7)
			result -= 36;
	}
	return result;
}

const int8_t QrCode::ECC_CODEWORDS_PER_BLOCK[4][41] = {
	{-1,  7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
	{-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28},
	{-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
	{-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
};

const int8_t QrCode::NUM_ERROR_CORRECTION_BLOCKS[4][41] = {
	{-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8,  9,  9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},
	{-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},
	{-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},
	{-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81},
};

}
