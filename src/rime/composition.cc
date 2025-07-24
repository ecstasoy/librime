//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-06-19 GONG Chen <chen.sst@gmail.com>
//
#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <rime/candidate.h>
#include <rime/composition.h>
#include <rime/menu.h>
#include <utf8.h>

namespace rime {

bool Composition::HasFinishedComposition() const {
  if (empty())
    return false;
  size_t k = size() - 1;
  if (k > 0 && at(k).start == at(k).end)
    --k;
  return at(k).status >= Segment::kSelected;
}

Preedit Composition::GetPreedit(const string& full_input,
                                size_t caret_pos,
                                const string& caret) const {
  Preedit preedit;
  preedit.caret_pos = string::npos;
  size_t start = 0;
  size_t end = 0;
  for (size_t i = 0; i < size(); ++i) {
    start = end;
    if (caret_pos == start) {
      preedit.caret_pos = preedit.text.length();
    }
    auto cand = at(i).GetSelectedCandidate();
    if (i < size() - 1) {  // converted
      if (cand) {
        end = cand->end();
        preedit.text += cand->text();
      } else {  // raw input
        end = at(i).end;
        if (!at(i).HasTag("phony")) {
          preedit.text += input_.substr(start, end - start);
        }
      }
    } else {  // highlighted
      preedit.sel_start = preedit.text.length();
      preedit.sel_end = string::npos;
      if (cand && !cand->preedit().empty()) {
        end = cand->end();
        auto caret_placeholder = cand->preedit().find('\t');
        if (caret_placeholder != string::npos) {
          preedit.text += cand->preedit().substr(0, caret_placeholder);
          // the part after caret is considered prompt string,
          // show it only when the caret is at the end of input.
          if (caret_pos == end && end == full_input.length()) {
            preedit.sel_end = preedit.sel_start + caret_placeholder;
            preedit.caret_pos = preedit.sel_end;
            preedit.text += cand->preedit().substr(caret_placeholder + 1);
          }
        } else {
          preedit.text += cand->preedit();
        }
      } else {
        end = at(i).end;
        preedit.text += input_.substr(start, end - start);
      }
      if (preedit.sel_end == string::npos) {
        preedit.sel_end = preedit.text.length();
      }
    }
  }
  if (end < input_.length()) {
    preedit.text += input_.substr(end);
    end = input_.length();
  }
  if (preedit.caret_pos == string::npos) {
    preedit.caret_pos = preedit.text.length();
  }
  if (end < full_input.length()) {
    preedit.text += full_input.substr(end);
  }
  // insert soft cursor and prompt string.
  auto prompt = caret + GetPrompt();
  if (!prompt.empty()) {
    preedit.text.insert(preedit.caret_pos, prompt);
    if (preedit.caret_pos < preedit.sel_start) {
      preedit.sel_start += prompt.length();
    }
    if (preedit.caret_pos < preedit.sel_end) {
      preedit.sel_end += prompt.length();
    }
  }
  return preedit;
}

string Composition::GetPrompt() const {
  return empty() ? string() : back().prompt;
}

string Composition::GetCommitText() const {
  string result;
  size_t end = 0;
  for (const Segment& seg : *this) {
    if (auto cand = seg.GetSelectedCandidate()) {
      end = cand->end();
      result += cand->text();
    } else {
      end = seg.end;
      if (!seg.HasTag("phony")) {
        result += input_.substr(seg.start, seg.end - seg.start);
      }
    }
  }
  if (input_.length() > end) {
    result += input_.substr(end);
  }
  return result;
}

string Composition::GetScriptText(bool keep_selection) const {
  string result;
  size_t start = 0;
  size_t end = 0;
  for (const Segment& seg : *this) {
    auto cand = seg.GetSelectedCandidate();
    start = end;
    end = cand ? cand->end() : seg.end;
    if (keep_selection && cand && !cand->text().empty() &&
        seg.status >= Segment::kSelected)
      result += cand->text();
    else if (cand && !cand->preedit().empty())
      result += boost::erase_first_copy(cand->preedit(), "\t");
    else
      result += input_.substr(start, end - start);
  }
  if (input_.length() > end) {
    result += input_.substr(end);
  }
  return result;
}

string Composition::GetDebugText() const {
  string result;
  int i = 0;
  for (const Segment& seg : *this) {
    if (i++ > 0)
      result += "|";
    if (!seg.tags.empty()) {
      result += "{";
      int j = 0;
      for (const string& tag : seg.tags) {
        if (j++ > 0)
          result += ",";
        result += tag;
      }
      result += "}";
    }
    result += input_.substr(seg.start, seg.end - seg.start);
    if (auto cand = seg.GetSelectedCandidate()) {
      result += "=>";
      result += cand->text();
    }
  }
  return result;
}

string Composition::GetTextBefore(size_t pos) const {
  if (empty())
    return string();
  for (const auto& seg : boost::adaptors::reverse(*this)) {
    if (seg.end <= pos) {
      if (auto cand = seg.GetSelectedCandidate()) {
        return cand->text();
      }
    }
  }
  return string();
}

Composition::CandidatePreview Composition::GetCandidatePreview(size_t candidate_index) const {
  DLOG(INFO) << "GetCandidatePreview called with index: " << candidate_index;
  if (empty()) {
    return CandidatePreview{};
  }
  DLOG(INFO) << "Composition has " << size() << " segments";
  return GetCandidatePreviewForCurrentSegment(candidate_index);
}

Composition::CandidatePreview Composition::GetCandidatePreviewForCurrentSegment(size_t candidate_index) const {
  DLOG(INFO) << "GetCandidatePreviewForCurrentSegment called with index: " << candidate_index;
  CandidatePreview preview;
  preview.consumed_length = 0;
  preview.candidate_start = 0;
  preview.candidate_end = 0;
  preview.has_remaining_input = false;

  if (empty()) {
    return preview;
  }

  const Segment& current_seg = at(size() - 1);
  DLOG(INFO) << "Current segment: [" << current_seg.start << ", " << current_seg.end << "]";
  
  if (!current_seg.menu) {
    return preview;
  }

  auto candidate = current_seg.GetCandidateAt(candidate_index);
  if (!candidate) {
    return preview;
  }
  
  DLOG(INFO) << "Found candidate: '" << candidate->text() << "' at [" << candidate->start() << ", " << candidate->end() << "]";

  string result;
  size_t end = 0;
  for (size_t i = 0; i < size() - 1; i++) {
    const Segment& seg = at(i);
    if (seg.status >= Segment::kSelected) {
      if (auto cand = seg.GetSelectedCandidate()) {
        end = cand->end();
        result += cand->text();
      } else {
        end = seg.end;
        string raw_text = input_.substr(seg.start, seg.end - seg.start);
        result += raw_text;
      }
    }
  }

  // append the selected candidate text
  preview.candidate_start = result.length();

  result += candidate->text();
  preview.candidate_end = result.length();
  preview.consumed_length = candidate->end() - current_seg.start;

  size_t candidate_end_pos = candidate->end();
  if (input_.length() > candidate_end_pos) {
    string remaining = input_.substr(candidate_end_pos);
    result += remaining;
    preview.has_remaining_input = true;
  }

  preview.preview_text = result;

  preview.candidate_start_index = static_cast<int>(
    utf8::unchecked::distance(result.c_str(), result.c_str() + preview.candidate_start)
  );

  preview.candidate_end_index = static_cast<int>(
    utf8::unchecked::distance(result.c_str(), result.c_str() + preview.candidate_end)
  );
  
  DLOG(INFO) << "Final preview: '" << preview.preview_text << "'";
  DLOG(INFO) << "Candidate byte range: [" << preview.candidate_start << ", " << preview.candidate_end << "]";
  DLOG(INFO) << "Candidate char range: [" << preview.candidate_start_index << ", " << preview.candidate_end_index << "]";
  
  return preview;
}

}  // namespace rime
