#include "PreviewBlockLocator.h"

#include <cstring>

PreviewBlockLocator::PreviewBlockLocator(const char* anchorId, const IsBlockTagFn isBlockTag)
    : anchorId_(anchorId), isBlockTag_(isBlockTag) {
  if (!anchorId_ || anchorId_[0] == '\0' || !isBlockTag_) {
    return;
  }

  parser_ = XML_ParserCreate(nullptr);
  if (!parser_) {
    return;
  }
  XML_SetUserData(parser_, this);
  XML_SetElementHandler(parser_, handleStartElement, handleEndElement);
}

PreviewBlockLocator::~PreviewBlockLocator() {
  if (parser_) {
    XML_ParserFree(parser_);
  }
}

bool PreviewBlockLocator::feed(const char* data, const int len, const bool isFinal) {
  if (!parser_ || done()) {
    return true;
  }

  const XML_Status status = XML_Parse(parser_, data, len, isFinal ? XML_TRUE : XML_FALSE);
  if (status != XML_STATUS_OK) {
    // XML_StopParser in the start handler reports XML_ERROR_ABORTED; that is our success path.
    return XML_GetErrorCode(parser_) == XML_ERROR_ABORTED;
  }
  return true;
}

void XMLCALL PreviewBlockLocator::handleStartElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<PreviewBlockLocator*>(userData);
  self->ordinal_ += 1;

  const char* id = nullptr;
  for (int i = 0; atts && atts[i]; i += 2) {
    if (strcmp(atts[i], "id") == 0) {
      id = atts[i + 1];
      break;
    }
  }

  if (id && strcmp(id, self->anchorId_) == 0) {
    // A block that carries the anchor itself is already the right starting point. An inline anchor
    // resolves outwards to its innermost enclosing block so that block's leading text is kept.
    self->startOrdinal_ = (self->isBlockTag_(name) || self->openBlockCount_ == 0)
                              ? self->ordinal_
                              : self->openBlockOrdinals_[self->openBlockCount_ - 1];
    XML_StopParser(self->parser_, XML_FALSE);
    return;
  }

  if (self->isBlockTag_(name) && self->openBlockCount_ < MAX_BLOCK_NESTING) {
    self->openBlockOrdinals_[self->openBlockCount_] = self->ordinal_;
    self->openBlockDepths_[self->openBlockCount_] = self->depth_;
    self->openBlockCount_ += 1;
  }
  self->depth_ += 1;
}

void XMLCALL PreviewBlockLocator::handleEndElement(void* userData, const XML_Char* name) {
  (void)name;
  auto* self = static_cast<PreviewBlockLocator*>(userData);
  if (self->depth_ > 0) {
    self->depth_ -= 1;
  }
  // The innermost open block closes when depth returns to the depth it was opened at.
  if (self->openBlockCount_ > 0 && self->openBlockDepths_[self->openBlockCount_ - 1] == self->depth_) {
    self->openBlockCount_ -= 1;
  }
}
