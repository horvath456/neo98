// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/text_edit.h"
#include <cstdio>
#include <cstring>
#include <string>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "Editor check failed at %d: %s\n", __LINE__, #x); return 1; } } while (0)
static int breaks(void *p,const char *,unsigned current,unsigned,unsigned code)
{
    ++*static_cast<unsigned *>(p);
    if(code==1)return current<3?3:current<6?6:8;
    return current>6?6:current>3?3:0;
}
struct Reenter { TextEdit *edit; bool done; };
static int reenter(void *p,const char *,unsigned current,unsigned,unsigned)
{
    auto &r=*static_cast<Reenter *>(p);
    if(!r.done){r.done=true;r.edit->Reset("ok");}
    return current;
}
int main()
{
    // Win98's single-line and multiline EDIT differ at an exact glyph
    // midpoint. Dragging back across the anchor must preserve its endpoint.
    {
        TextEdit points;int cells[256];for(auto &width:cells)width=8;
        points.Metrics(cells,16,160,40,160);points.Reset("alpha beta");
        points.SelectAt(12,4,false);CHECK(points.Cursor()==2);
        points.SelectAt(12,4,false,true);CHECK(points.Cursor()==1);
        points.SelectAt(35,4,true,true);CHECK(points.SelectionStart()==1&&points.SelectionEnd()==4);
        points.SelectAt(0,4,true,true);CHECK(points.SelectionStart()==0&&points.SelectionEnd()==1);
        points.SelectAt(13,4,false,true);CHECK(points.Cursor()==2);
    }
    TextEdit fit;
    int fixed[256];for(auto &width:fixed)width=6;
    fit.Metrics(fixed,10,30,20,30);fit.Reset("abc");fit.SetSelection(1,2);
    CHECK(fit.FitInsertion("123456",6,false,false,false)==3);
    CHECK(fit.FitInsertion("123456",6,false,true,false)==6);
    CHECK(!strcmp(fit.Data(),"abc")&&fit.SelectionStart()==1&&fit.SelectionEnd()==2&&!fit.CanUndo());
    fit.Reset("a\nb");fit.SetSelection(3,3);
    CHECK(fit.FitInsertion("\nc",2,true,false,false)==0);
    CHECK(fit.FitInsertion("\nc",2,true,false,true)==2);
    CHECK(fit.FitInsertion("c",1,true,false,false)==1);
    CHECK(!strcmp(fit.Data(),"a\nb")&&!fit.CanUndo());
    CHECK(fit.Insert("c",1)&&fit.Undo()&&!strcmp(fit.Data(),"a\nb"));
    TextEdit edit;
    int widths[256]; for (auto &v : widths) v = 6;
    widths['W'] = 10; widths['i'] = 2; widths[' '] = 3;
    edit.Metrics(widths, 13, 30, 26); edit.Reset("Wi Wi Wi");
    CHECK(edit.LineCount() == 2 && edit.GetLine(0).length == 6 && edit.GetLine(0).width == 30);
    edit.Move(TextEdit::End); CHECK(edit.Cursor() == 6 && edit.CursorLine() == 0 && edit.CursorX() == 30);
    edit.Move(TextEdit::Down); CHECK(edit.Cursor() == 8 && edit.CursorLine() == 1);
    edit.Move(TextEdit::Home, true);
    char clipboard[TextEdit::Capacity]; CHECK(edit.Copy(clipboard) == 2 && !strcmp(clipboard, "Wi"));
    CHECK(edit.Insert("ARM", 3) && !strcmp(edit.Data(), "Wi Wi ARM"));
    CHECK(edit.Undo() && !strcmp(edit.Data(), "Wi Wi Wi") && edit.SelectionEnd() - edit.SelectionStart() == 2);
    CHECK(edit.Undo() && !strcmp(edit.Data(), "Wi Wi ARM")); // one-level undo/redo
    edit.SelectAll(); CHECK(edit.Insert("\n\n", 2));
    CHECK(edit.LineCount() == 3 && edit.FirstLine() == 1 && edit.CursorLine() == 2);
    CHECK(edit.Erase(false) && edit.Length() == 1 && edit.LineCount() == 2);
    CHECK(edit.Undo() && edit.Length() == 2 && edit.Cursor() == 2 && edit.SelectionStart() == 2);
    edit.Reset("Wi Wi");
    edit.SelectAt(9, 0, false); CHECK(edit.Cursor() == 1);
    edit.SelectAt(27, 0, true); CHECK(edit.SelectionStart() == 1 && edit.SelectionEnd() == 5);
    CHECK(edit.Erase(true) && !strcmp(edit.Data(), "W"));
    CHECK(edit.Undo() && !strcmp(edit.Data(), "Wi Wi"));
    edit.Move(TextEdit::DocumentHome); CHECK(!edit.Erase(false));
    edit.Move(TextEdit::DocumentEnd); CHECK(!edit.Erase(true));
    edit.SelectAll();
    std::string full(TextEdit::Capacity - 1, 'A');
    CHECK(edit.Insert(full.data(), full.size()));
    CHECK(!edit.Insert("B", 1) && edit.Length() == TextEdit::Capacity - 1 && !strcmp(edit.Data(), full.c_str()));
    edit.Metrics(widths, 13, 1, 13);
    CHECK(edit.LineCount() == TextEdit::Capacity - 1 && edit.FirstLine() == TextEdit::Capacity - 2);
    edit.SelectAll(); CHECK(edit.Insert(std::string(TextEdit::Capacity - 1, '\n').c_str(), TextEdit::Capacity - 1));
    CHECK(edit.LineCount() == TextEdit::Capacity && edit.FirstLine() == TextEdit::Capacity - 1);
    edit.Scroll(-int(TextEdit::Capacity)); CHECK(edit.FirstLine() == 0);
    edit.Move(TextEdit::DocumentHome); CHECK(edit.CursorLine() == 0);
    edit.SelectAll(); CHECK(edit.Erase(false) && edit.LineCount() == 1 && !edit.Length());
    edit.Metrics(widths, 13, 300, 26); edit.Reset("a\tb");
    CHECK(edit.CursorX() == 0); edit.Move(TextEdit::End); CHECK(edit.CursorX() == 38);
    const int tabs[] = {80}; CHECK(edit.SetTabStops(tabs, 1));
    CHECK(edit.CursorX() == 86);
    edit.SetSelection(1,2); CHECK(edit.PrefixWidth(0,1)==6&&edit.PrefixWidth(0,2)==80);
    edit.Reset("a\nb\nc\nd\ne"); edit.Move(TextEdit::DocumentHome);
    CHECK(edit.ScrollCommand(1) == 0x10001 && edit.ScrollPosition() == 1);
    CHECK(edit.ScrollCommand(2) == 0x1ffff && edit.ScrollPosition() == 0);
    CHECK(edit.ScrollCommand(99) == 0 && edit.ScrollPosition() == 0);
    edit.ScrollVertical(1,0); CHECK(edit.FirstLine()==1);
    edit.ScrollVertical(0,0); CHECK(edit.FirstLine()==0);
    edit.ScrollVertical(3,0); CHECK(edit.FirstLine()==1); // two visible lines, one-line overlap
    edit.ScrollVertical(5,3); CHECK(edit.FirstLine()==3);
    edit.ScrollVertical(4,65535); CHECK(edit.FirstLine()==3); // clamp to the last page
    edit.ScrollVertical(2,0); CHECK(edit.FirstLine()==2);
    edit.ScrollVertical(4,0); CHECK(edit.FirstLine()==0);
    edit.ScrollVertical(7,0); CHECK(edit.FirstLine()==0); // USER ignores SB_BOTTOM

    edit.Reset("01234567890123456789");
    edit.Metrics(widths, 10, 30000, 20, 30);
    edit.ScrollColumns(2); CHECK(edit.HorizontalPosition() == 12);
    edit.ScrollHorizontal(3, 0); CHECK(edit.HorizontalPosition() == 42);
    edit.ScrollHorizontal(5, 17); CHECK(edit.HorizontalPosition() == 17);
    edit.ScrollHorizontal(6, 0); CHECK(edit.HorizontalPosition() == 0);
    edit.ScrollHorizontal(7, 0); CHECK(edit.HorizontalPosition() == 90);
    edit.ScrollColumns(-40000); CHECK(edit.HorizontalPosition() == 0);
    edit.ScrollColumns(40000); CHECK(edit.HorizontalPosition() == 90);
    edit.ScrollColumns(-2147483647 - 1); CHECK(edit.HorizontalPosition() == 0);
    edit.ScrollColumns(2147483647); CHECK(edit.HorizontalPosition() == 90);
    edit.SetSelection(0, 0); CHECK(edit.HorizontalPosition() == 0);
    edit.SetSelection(edit.Length(), edit.Length()); CHECK(edit.HorizontalPosition() == 90);
    edit.SelectAt(0, 0, false); CHECK(edit.Cursor() >= 14 && edit.Cursor() <= 15);
    widths['#'] = 7;
    edit.Metrics(widths, 10, 30000, 20, 20); edit.Reset("iiiWWW");
    edit.SetPasswordCharacter('#');
    edit.Move(TextEdit::DocumentEnd);
    CHECK(edit.CursorX() == 42 && edit.HorizontalPosition() == 22);
    edit.SetSelection(0, 0);
    edit.SelectAt(10, 0, false);
    CHECK(edit.Cursor() == 1 && edit.CursorX() == 7 && edit.HorizontalPosition() == 0);
    edit.SetPasswordCharacter(0);
    CHECK(edit.CursorX() == 2);
    unsigned break_calls=0;
    edit.Metrics(widths,10,20,20,20);edit.Reset("aa-bb cc");edit.SetWordBreak(&break_calls,breaks);
    CHECK(edit.LineCount()>=3&&edit.GetLine(0).length==3&&break_calls);
    edit.SetSelection(0,0);edit.Move(TextEdit::WordRight);CHECK(edit.Cursor()==3);
    edit.Move(TextEdit::WordRight);CHECK(edit.Cursor()==6);
    edit.Move(TextEdit::WordLeft);CHECK(edit.Cursor()==3);
    Reenter reentrant{&edit,false};edit.Reset("long long long");edit.SetWordBreak(&reentrant,reenter);
    CHECK(reentrant.done&&!strcmp(edit.Data(),"ok")&&edit.LineCount()==1);
    // ES_RIGHT and ES_CENTER: the hour field of Date/Time is
    // 50030006h, ES_MULTILINE | ES_RIGHT; its "2" stands at the right edge.
    edit.SetWordBreak(nullptr,nullptr);
    for (auto &v : widths) v = 6;
    edit.Metrics(widths,13,40,13,40);edit.Reset("2");edit.SetAlignment(TextEdit::AlignRight);
    CHECK(edit.LineOffset(0)==33);
    edit.SetSelection(1,1);CHECK(edit.CursorX()==39&&edit.HorizontalPosition()==0);
    edit.SelectAt(20,0,false);CHECK(edit.Cursor()==0);
    edit.SelectAt(37,0,false);CHECK(edit.Cursor()==1);
    edit.SetAlignment(TextEdit::AlignCenter);CHECK(edit.LineOffset(0)==16);
    edit.SetSelection(0,0);CHECK(edit.CursorX()==16);
    edit.Metrics(widths,13,30000,13,40);                    // ES_AUTOHSCROLL: no wrap
    edit.Reset("123456789");CHECK(edit.LineOffset(0)==0);   // wider than the field: from the left
    edit.SetAlignment(TextEdit::AlignLeft);edit.Reset("2");CHECK(edit.LineOffset(0)==0);
    std::puts("PASS: proportional wrap, password geometry, reentrant word-break wrap/navigation, selection, undo, tabs, limits and alignment");
}
