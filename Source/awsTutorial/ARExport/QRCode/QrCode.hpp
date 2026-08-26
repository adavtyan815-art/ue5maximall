/* 
 * QR Code generator library for Unreal Engine
 * 
 * Adapted from Project Nayuki (MIT License)
 * https://www.nayuki.io/page/qr-code-generator-library
 */

#pragma once

#include "CoreMinimal.h"

namespace qrcodegen {

class QrSegment final {
public:
	enum class Mode {
		NUMERIC,
		ALPHANUMERIC,
		BYTE,
		KANJI,
		ECI,
	};

	static QrSegment makeBytes(const TArray<uint8>& data);
	static QrSegment makeNumeric(const char *digits);
	static QrSegment makeAlphanumeric(const char *text);
	static TArray<QrSegment> makeSegments(const char *text);
	static QrSegment makeEci(long assignVal);

	QrSegment(Mode md, int numCh, const TArray<bool>& dt);
	QrSegment(Mode md, int numCh, TArray<bool>&& dt);

	Mode getMode() const;
	int getNumChars() const;
	const TArray<bool>& getData() const;

	static int getTotalBits(const TArray<QrSegment>& segs, int version);

private:
	Mode mode;
	int numChars;
	TArray<bool> data;
};

class QrCode final {
public:
	enum class Ecc {
		LOW = 0,
		MEDIUM,
		QUARTILE,
		HIGH,
	};

	static QrCode encodeText(const char *text, Ecc ecl);
	static QrCode encodeBinary(const TArray<uint8>& data, Ecc ecl);
	static QrCode encodeSegments(const TArray<QrSegment>& segs, Ecc ecl,
		int minVersion = 1, int maxVersion = 40, int mask = -1, bool boostEcl = true);

	QrCode(int ver, Ecc ecl, const TArray<uint8>& dataCodewords, int msk);
	QrCode();

	int getVersion() const;
	int getSize() const;
	Ecc getErrorCorrectionLevel() const;
	int getMask() const;
	bool getModule(int x, int y) const;
	bool isValid() const { return size > 0; }

	static constexpr int MIN_VERSION =  1;
	static constexpr int MAX_VERSION = 40;

private:
	int version = 0;
	int size = 0;
	Ecc errorCorrectionLevel = Ecc::MEDIUM;
	int mask = 0;
	TArray<TArray<bool>> modules;
	TArray<TArray<bool>> isFunction;

	void drawFunctionPatterns();
	void drawFormatBits(int msk);
	void drawVersion();
	void drawFinderPattern(int x, int y);
	void drawAlignmentPattern(int x, int y);
	void setFunctionModule(int x, int y, bool isBlack);
	bool module(int x, int y, bool isFunc) const;

	TArray<uint8> addEccAndInterleave(const TArray<uint8>& data) const;
	void drawCodewords(const TArray<uint8>& data);
	void applyMask(int msk);
	long getPenaltyScore() const;

	static bool getBit(int x, int i);
	static int getNumDataCodewords(int ver, Ecc ecl);
	static int getNumRawDataModules(int ver);
	static const int8_t ECC_CODEWORDS_PER_BLOCK[4][41];
	static const int8_t NUM_ERROR_CORRECTION_BLOCKS[4][41];
};

}
