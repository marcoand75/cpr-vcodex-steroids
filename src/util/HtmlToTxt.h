#pragma once

#include <HalStorage.h>
#include <cstring>

class HtmlToTxt {
public:
  bool convert(HalFile& in, const char* outPath);

private:
  // Tag parsing state
  bool inTag;
  bool closingTag;
  bool invalidTag;
  bool inPre;
  int skipStack;
  char tagBuf[24];
  int tagLen;
  int tagScanCount;

  // Text formatting state
  bool lastWasSpace;
  bool lastWasNewline;
  int pendingBlankLines;

  static constexpr int MAX_LIST_DEPTH = 8;
  static constexpr int MAX_BLOCKQUOTE = 8;
  static constexpr int MAX_TAG_SCAN = 48;

  struct ListState { bool ordered; int counter; };
  ListState listStack[MAX_LIST_DEPTH];
  int listDepth;
  int blockquoteDepth;
  bool inTableRow;
  bool firstCellInRow;

  // Output buffering — batch writes to SD for performance
  static constexpr int OUT_BUF_SIZE = 512;
  char outBuf[OUT_BUF_SIZE];
  int outBufLen;
  HalFile* outFile; // current output file for flushing

  void flushOut();
  void writeOut(char c);
  void writeStrOut(const char* s);

  void reset();
  bool isSkipping();
  void newline(int maxBlank = 1);
  void writeIndent();
  void handleTag();
  bool isSkipTag(const char* tag);
};
