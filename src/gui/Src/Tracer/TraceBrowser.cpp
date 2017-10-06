#include "TraceBrowser.h"
#include "TraceFileReader.h"
#include "TraceFileSearch.h"
#include "RichTextPainter.h"
#include "main.h"
#include "BrowseDialog.h"
#include "QBeaEngine.h"
#include "GotoDialog.h"
#include "LineEditDialog.h"
#include "WordEditDialog.h"
#include "CachedFontMetrics.h"
#include "BreakpointMenu.h"

TraceBrowser::TraceBrowser(QWidget* parent) : AbstractTableView(parent)
{
    mTraceFile = nullptr;
    addColumnAt(getCharWidth() * 2 * 8 + 8, "", false); //index
    addColumnAt(getCharWidth() * 2 * sizeof(dsint) + 8, "", false); //address
    addColumnAt(getCharWidth() * 2 * 12 + 8, "", false); //bytes
    addColumnAt(getCharWidth() * 40, "", false); //disassembly
    addColumnAt(1000, "", false); //comments

    setShowHeader(false); //hide header

    mSelection.firstSelectedIndex = 0;
    mSelection.fromIndex = 0;
    mSelection.toIndex = 0;
    setRowCount(0);
    mRvaDisplayBase = 0;
    mRvaDisplayEnabled = false;

    int maxModuleSize = (int)ConfigUint("Disassembler", "MaxModuleSize");
    mDisasm = new QBeaEngine(maxModuleSize);
    mHighlightingMode = false;
    mPermanentHighlightingMode = false;

    setupRightClickContextMenu();

    Initialize();

    connect(Bridge::getBridge(), SIGNAL(updateTraceBrowser()), this, SLOT(updateSlot()));
    connect(Bridge::getBridge(), SIGNAL(openTraceFile(const QString &)), this, SLOT(openSlot(const QString &)));
}

TraceBrowser::~TraceBrowser()
{
    delete mDisasm;
}

QString TraceBrowser::getAddrText(dsint cur_addr, char label[MAX_LABEL_SIZE], bool getLabel)
{
    QString addrText = "";
    if(mRvaDisplayEnabled) //RVA display
    {
        dsint rva = cur_addr - mRvaDisplayBase;
        if(rva == 0)
        {
#ifdef _WIN64
            addrText = "$ ==>            ";
#else
            addrText = "$ ==>    ";
#endif //_WIN64
        }
        else if(rva > 0)
        {
#ifdef _WIN64
            addrText = "$+" + QString("%1").arg(rva, -15, 16, QChar(' ')).toUpper();
#else
            addrText = "$+" + QString("%1").arg(rva, -7, 16, QChar(' ')).toUpper();
#endif //_WIN64
        }
        else if(rva < 0)
        {
#ifdef _WIN64
            addrText = "$-" + QString("%1").arg(-rva, -15, 16, QChar(' ')).toUpper();
#else
            addrText = "$-" + QString("%1").arg(-rva, -7, 16, QChar(' ')).toUpper();
#endif //_WIN64
        }
    }
    addrText += ToPtrString(cur_addr);
    char label_[MAX_LABEL_SIZE] = "";
    if(getLabel && DbgGetLabelAt(cur_addr, SEG_DEFAULT, label_)) //has label
    {
        char module[MAX_MODULE_SIZE] = "";
        if(DbgGetModuleAt(cur_addr, module) && !QString(label_).startsWith("JMP.&"))
            addrText += " <" + QString(module) + "." + QString(label_) + ">";
        else
            addrText += " <" + QString(label_) + ">";
    }
    else
        *label_ = 0;
    if(label)
        strcpy_s(label, MAX_LABEL_SIZE, label_);
    return addrText;
}

QString TraceBrowser::paintContent(QPainter* painter, dsint rowBase, int rowOffset, int col, int x, int y, int w, int h)
{
    if(!mTraceFile || mTraceFile->Progress() != 100)
    {
        return "";
    }
    if(mTraceFile->isError())
    {
        GuiAddLogMessage(tr("An error occured when reading trace file.\r\n").toUtf8().constData());
        mTraceFile->Close();
        delete mTraceFile;
        mTraceFile = nullptr;
        setRowCount(0);
        return "";
    }
    if(mHighlightingMode)
    {
        QPen pen(mInstructionHighlightColor);
        pen.setWidth(2);
        painter->setPen(pen);
        QRect rect = viewport()->rect();
        rect.adjust(1, 1, -1, -1);
        painter->drawRect(rect);
    }

    int index = rowBase + rowOffset;
    duint cur_addr;
    cur_addr = mTraceFile->Registers(index).regcontext.cip;
    bool wIsSelected = (index >= mSelection.fromIndex && index <= mSelection.toIndex);
    if(wIsSelected)
    {
        painter->fillRect(QRect(x, y, w, h), QBrush(selectionColor));
    }
    if(index >= mTraceFile->Length())
        return "";
    switch(col)
    {
    case 0: //index
    {
        return getIndexText(index);
    }

    case 1: //address
    {
        QString addrText;
        char label[MAX_LABEL_SIZE] = "";
        if(!DbgIsDebugging())
        {
            addrText = ToPtrString(cur_addr);
            goto NotDebuggingLabel;
        }
        else
            addrText = getAddrText(cur_addr, label, true);
        BPXTYPE bpxtype = DbgGetBpxTypeAt(cur_addr);
        bool isbookmark = DbgGetBookmarkAt(cur_addr);
        //todo: cip
        {
            if(!isbookmark) //no bookmark
            {
                if(*label) //label
                {
                    if(bpxtype == bp_none) //label only : fill label background
                    {
                        painter->setPen(mLabelColor); //red -> address + label text
                        painter->fillRect(QRect(x, y, w, h), QBrush(mLabelBackgroundColor)); //fill label background
                    }
                    else //label + breakpoint
                    {
                        if(bpxtype & bp_normal) //label + normal breakpoint
                        {
                            painter->setPen(mBreakpointColor);
                            painter->fillRect(QRect(x, y, w, h), QBrush(mBreakpointBackgroundColor)); //fill red
                        }
                        else if(bpxtype & bp_hardware) //label + hardware breakpoint only
                        {
                            painter->setPen(mHardwareBreakpointColor);
                            painter->fillRect(QRect(x, y, w, h), QBrush(mHardwareBreakpointBackgroundColor)); //fill ?
                        }
                        else //other cases -> do as normal
                        {
                            painter->setPen(mLabelColor); //red -> address + label text
                            painter->fillRect(QRect(x, y, w, h), QBrush(mLabelBackgroundColor)); //fill label background
                        }
                    }
                }
                else //no label
                {
                    if(bpxtype == bp_none) //no label, no breakpoint
                    {
NotDebuggingLabel:
                        QColor background;
                        if(wIsSelected)
                        {
                            background = mSelectedAddressBackgroundColor;
                            painter->setPen(mSelectedAddressColor); //black address (DisassemblySelectedAddressColor)
                        }
                        else
                        {
                            background = mAddressBackgroundColor;
                            painter->setPen(mAddressColor); //DisassemblyAddressColor
                        }
                        if(background.alpha())
                            painter->fillRect(QRect(x, y, w, h), QBrush(background)); //fill background
                    }
                    else //breakpoint only
                    {
                        if(bpxtype & bp_normal) //normal breakpoint
                        {
                            painter->setPen(mBreakpointColor);
                            painter->fillRect(QRect(x, y, w, h), QBrush(mBreakpointBackgroundColor)); //fill red
                        }
                        else if(bpxtype & bp_hardware) //hardware breakpoint only
                        {
                            painter->setPen(mHardwareBreakpointColor);
                            painter->fillRect(QRect(x, y, w, h), QBrush(mHardwareBreakpointBackgroundColor)); //fill red
                        }
                        else //other cases (memory breakpoint in disassembly) -> do as normal
                        {
                            QColor background;
                            if(wIsSelected)
                            {
                                background = mSelectedAddressBackgroundColor;
                                painter->setPen(mSelectedAddressColor); //black address (DisassemblySelectedAddressColor)
                            }
                            else
                            {
                                background = mAddressBackgroundColor;
                                painter->setPen(mAddressColor);
                            }
                            if(background.alpha())
                                painter->fillRect(QRect(x, y, w, h), QBrush(background)); //fill background
                        }
                    }
                }
            }
            else //bookmark
            {
                if(*label) //label + bookmark
                {
                    if(bpxtype == bp_none) //label + bookmark
                    {
                        painter->setPen(mLabelColor); //red -> address + label text
                        painter->fillRect(QRect(x, y, w, h), QBrush(mBookmarkBackgroundColor)); //fill label background
                    }
                    else //label + breakpoint + bookmark
                    {
                        QColor color = mBookmarkBackgroundColor;
                        if(!color.alpha()) //we don't want transparent text
                            color = mAddressColor;
                        painter->setPen(color);
                        if(bpxtype & bp_normal) //label + bookmark + normal breakpoint
                        {
                            painter->fillRect(QRect(x, y, w, h), QBrush(mBreakpointBackgroundColor)); //fill red
                        }
                        else if(bpxtype & bp_hardware) //label + bookmark + hardware breakpoint only
                        {
                            painter->fillRect(QRect(x, y, w, h), QBrush(mHardwareBreakpointBackgroundColor)); //fill ?
                        }
                    }
                }
                else //bookmark, no label
                {
                    if(bpxtype == bp_none) //bookmark only
                    {
                        painter->setPen(mBookmarkColor); //black address
                        painter->fillRect(QRect(x, y, w, h), QBrush(mBookmarkBackgroundColor)); //fill bookmark color
                    }
                    else //bookmark + breakpoint
                    {
                        QColor color = mBookmarkBackgroundColor;
                        if(!color.alpha()) //we don't want transparent text
                            color = mAddressColor;
                        painter->setPen(color);
                        if(bpxtype & bp_normal) //bookmark + normal breakpoint
                        {
                            painter->fillRect(QRect(x, y, w, h), QBrush(mBreakpointBackgroundColor)); //fill red
                        }
                        else if(bpxtype & bp_hardware) //bookmark + hardware breakpoint only
                        {
                            painter->fillRect(QRect(x, y, w, h), QBrush(mHardwareBreakpointBackgroundColor)); //fill red
                        }
                        else //other cases (bookmark + memory breakpoint in disassembly) -> do as normal
                        {
                            painter->setPen(mBookmarkColor); //black address
                            painter->fillRect(QRect(x, y, w, h), QBrush(mBookmarkBackgroundColor)); //fill bookmark color
                        }
                    }
                }
            }
        }
        painter->drawText(QRect(x + 4, y, w - 4, h), Qt::AlignVCenter | Qt::AlignLeft, addrText);
    }
    return "";

    case 2: //opcode
    {
        RichTextPainter::List richBytes;
        RichTextPainter::CustomRichText_t curByte;
        RichTextPainter::CustomRichText_t space;
        unsigned char opcodes[16];
        int opcodeSize = 0;
        mTraceFile->OpCode(index, opcodes, &opcodeSize);
        space.text = " ";
        space.flags = RichTextPainter::FlagNone;
        space.highlightWidth = 1;
        space.highlightConnectPrev = true;
        curByte.flags = RichTextPainter::FlagAll;
        curByte.highlightWidth = 1;
        space.highlight = false;
        curByte.highlight = false;

        for(int i = 0; i < opcodeSize; i++)
        {
            if(i)
                richBytes.push_back(space);

            curByte.text = ToByteString(opcodes[i]);
            curByte.textColor = mBytesColor;
            curByte.textBackground = mBytesBackgroundColor;
            richBytes.push_back(curByte);
        }

        RichTextPainter::paintRichText(painter, x, y, getColumnWidth(col), getRowHeight(), 4, richBytes, mFontMetrics);
        return "";
    }

    case 3: //disassembly
    {
        RichTextPainter::List richText;
        unsigned char opcodes[16];
        int opcodeSize = 0;
        mTraceFile->OpCode(index, opcodes, &opcodeSize);

        Instruction_t inst = mDisasm->DisassembleAt(opcodes, opcodeSize, 0, mTraceFile->Registers(index).regcontext.cip, false);

        if(mHighlightToken.text.length())
            CapstoneTokenizer::TokenToRichText(inst.tokens, richText, &mHighlightToken);
        else
            CapstoneTokenizer::TokenToRichText(inst.tokens, richText, 0);
        RichTextPainter::paintRichText(painter, x + 0, y, getColumnWidth(col) - 0, getRowHeight(), 4, richText, mFontMetrics);
        return "";
    }

    case 4: //comments
    {
        if(DbgIsDebugging())
        {
            //TODO: draw arguments
            QString comment;
            bool autoComment = false;
            char label[MAX_LABEL_SIZE] = "";
            if(GetCommentFormat(cur_addr, comment, &autoComment))
            {
                QColor backgroundColor;
                if(autoComment)
                {
                    painter->setPen(mAutoCommentColor);
                    backgroundColor = mAutoCommentBackgroundColor;
                }
                else //user comment
                {
                    painter->setPen(mCommentColor);
                    backgroundColor = mCommentBackgroundColor;
                }

                int width = mFontMetrics->width(comment);
                if(width > w)
                    width = w;
                if(width)
                    painter->fillRect(QRect(x, y, width, h), QBrush(backgroundColor)); //fill comment color
                painter->drawText(QRect(x, y, width, h), Qt::AlignVCenter | Qt::AlignLeft, comment);
            }
            else if(DbgGetLabelAt(cur_addr, SEG_DEFAULT, label)) // label but no comment
            {
                QString labelText(label);
                QColor backgroundColor;
                painter->setPen(mLabelColor);
                backgroundColor = mLabelBackgroundColor;

                int width = mFontMetrics->width(labelText);
                if(width > w)
                    width = w;
                if(width)
                    painter->fillRect(QRect(x, y, width, h), QBrush(backgroundColor)); //fill comment color
                painter->drawText(QRect(x, y, width, h), Qt::AlignVCenter | Qt::AlignLeft, labelText);
            }
        }
        return "";
    }
    default:
        return "";
    }
}

void TraceBrowser::prepareData()
{
    auto viewables = getViewableRowsCount();
    int lines = 0;
    if(mTraceFile != nullptr)
    {
        if(mTraceFile->Progress() == 100)
        {
            if(mTraceFile->Length() < getTableOffset() + viewables)
                lines = mTraceFile->Length() - getTableOffset();
            else
                lines = viewables;
        }
    }
    setNbrOfLineToPrint(lines);
}

void TraceBrowser::setupRightClickContextMenu()
{
    mMenuBuilder = new MenuBuilder(this);
    QAction* toggleRunTrace = makeAction(DIcon("trace.png"), tr("Start Run Trace"), SLOT(toggleRunTraceSlot()));
    mMenuBuilder->addAction(toggleRunTrace, [toggleRunTrace](QMenu*)
    {
        if(DbgValFromString("tr.runtraceenabled()") == 1)
            toggleRunTrace->setText(tr("Stop Run Trace"));
        else
            toggleRunTrace->setText(tr("Start Run Trace"));
        return true;
    });
    mMenuBuilder->addAction(makeAction(DIcon("folder-horizontal-open.png"), tr("Open"), SLOT(openFileSlot())), [this](QMenu*)
    {
        return mTraceFile == nullptr;
    });
    mMenuBuilder->addAction(makeAction(DIcon("fatal-error.png"), tr("Close"), SLOT(closeFileSlot())), [this](QMenu*)
    {
        return mTraceFile != nullptr;
    });
    mMenuBuilder->addSeparator();
    auto isValid = [this](QMenu*)
    {
        return mTraceFile != nullptr && mTraceFile->Progress() == 100 && mTraceFile->Length() > 0;
    };
    MenuBuilder* copyMenu = new MenuBuilder(this, isValid);
    copyMenu->addAction(makeShortcutAction(DIcon("copy_address.png"), tr("Address"), SLOT(copyCipSlot()), "ActionCopyAddress"));
    copyMenu->addAction(makeAction(DIcon("copy_disassembly.png"), tr("Disassembly"), SLOT(copyDisassemblySlot())));
    copyMenu->addAction(makeAction(DIcon("copy_address.png"), tr("Index"), SLOT(copyIndexSlot())));
    mMenuBuilder->addMenu(makeMenu(DIcon("copy.png"), tr("&Copy")), copyMenu);
    mMenuBuilder->addAction(makeShortcutAction(DIcon(ArchValue("processor32.png", "processor64.png")), tr("Follow in Disassembly"), SLOT(followDisassemblySlot()), "ActionFollowDisasm"), isValid);

    mBreakpointMenu = new BreakpointMenu(this, getActionHelperFuncs(), [this, isValid]()
    {
        if(isValid(nullptr))
            return mTraceFile->Registers(getInitialSelection()).regcontext.cip;
        else
            return (duint)0;
    });
    mBreakpointMenu->build(mMenuBuilder);
    mMenuBuilder->addAction(makeShortcutAction(DIcon("comment.png"), tr("&Comment"), SLOT(setCommentSlot()), "ActionSetComment"), isValid);
    mMenuBuilder->addAction(makeShortcutAction(DIcon("highlight.png"), tr("&Highlighting mode"), SLOT(enableHighlightingModeSlot()), "ActionHighlightingMode"), isValid);
    MenuBuilder* gotoMenu = new MenuBuilder(this, isValid);
    gotoMenu->addAction(makeShortcutAction(DIcon("goto.png"), tr("Expression"), SLOT(gotoSlot()), "ActionGotoExpression"), isValid);
    gotoMenu->addAction(makeShortcutAction(DIcon("previous.png"), tr("Previous"), SLOT(gotoPreviousSlot()), "ActionGotoPrevious"), [this](QMenu*)
    {
        return mHistory.historyHasPrev();
    });
    gotoMenu->addAction(makeShortcutAction(DIcon("next.png"), tr("Next"), SLOT(gotoNextSlot()), "ActionGotoNext"), [this](QMenu*)
    {
        return mHistory.historyHasNext();
    });
    mMenuBuilder->addMenu(makeMenu(DIcon("goto.png"), tr("Go to")), gotoMenu);

    MenuBuilder* searchMenu = new MenuBuilder(this, isValid);
    searchMenu->addAction(makeAction(tr("Constant"), SLOT(searchConstantSlot())));
    searchMenu->addAction(makeAction(tr("Memory Reference"), SLOT(searchMemRefSlot())));
    mMenuBuilder->addMenu(makeMenu(DIcon("search.png"), tr("&Search")), searchMenu);

    // The following code adds a menu to view the information about currently selected instruction. When info box is completed, remove me.
    MenuBuilder* infoMenu = new MenuBuilder(this, [this, isValid](QMenu * menu)
    {
        duint MemoryAddress[MAX_MEMORY_OPERANDS];
        duint MemoryOldContent[MAX_MEMORY_OPERANDS];
        duint MemoryNewContent[MAX_MEMORY_OPERANDS];
        bool MemoryIsValid[MAX_MEMORY_OPERANDS];
        int MemoryOperandsCount;
        unsigned long long index;

        if(!isValid(nullptr))
            return false;
        index = getInitialSelection();
        MemoryOperandsCount = mTraceFile->MemoryAccessCount(index);
        if(MemoryOperandsCount > 0)
        {
            mTraceFile->MemoryAccessInfo(index, MemoryAddress, MemoryOldContent, MemoryNewContent, MemoryIsValid);
            bool RvaDisplayEnabled = mRvaDisplayEnabled;
            char nolabel[MAX_LABEL_SIZE];
            mRvaDisplayEnabled = false;
            for(int i = 0; i < MemoryOperandsCount; i++)
            {
                menu->addAction(QString("%1: %2 -> %3").arg(getAddrText(MemoryAddress[i], nolabel, false)).arg(ToPtrString(MemoryOldContent[i])).arg(ToPtrString(MemoryNewContent[i])));
            }
            mRvaDisplayEnabled = RvaDisplayEnabled;
            menu->addSeparator();
        }
#define addReg(str, reg) if(index + 1 < mTraceFile->Length()){menu->addAction(QString(str ":%1 -> %2").arg(ToPtrString(mTraceFile->Registers(index).regcontext.##reg)) \
    .arg(ToPtrString(mTraceFile->Registers(index + 1).regcontext.##reg))); }else{ menu->addAction(QString(str ":%1").arg(ToPtrString(mTraceFile->Registers(index).regcontext.##reg))); }
        addReg(ArchValue("EAX", "RAX"), cax)
        addReg(ArchValue("EBX", "RBX"), cbx)
        addReg(ArchValue("ECX", "RCX"), ccx)
        addReg(ArchValue("EDX", "RDX"), cdx)
        addReg(ArchValue("ESP", "RSP"), csp)
        addReg(ArchValue("EBP", "RBP"), cbp)
        addReg(ArchValue("ESI", "RSI"), csi)
        addReg(ArchValue("EDI", "RDI"), cdi)
#ifdef _WIN64
        addReg("R8", r8)
        addReg("R9", r9)
        addReg("R10", r10)
        addReg("R11", r11)
        addReg("R12", r12)
        addReg("R13", r13)
        addReg("R14", r14)
        addReg("R15", r15)
#endif //_WIN64
        addReg(ArchValue("EIP", "RIP"), cip)
        addReg(ArchValue("EFLAGS", "RFLAGS"), eflags)
        return true;
    });
    mMenuBuilder->addMenu(makeMenu(tr("Information")), infoMenu);
}

void TraceBrowser::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    mMenuBuilder->build(&menu);
    menu.exec(event->globalPos());
}

void TraceBrowser::mousePressEvent(QMouseEvent* event)
{
    duint index = getIndexOffsetFromY(transY(event->y())) + getTableOffset();
    if(getGuiState() == AbstractTableView::NoState && mTraceFile != nullptr && mTraceFile->Progress() == 100)
    {
        switch(event->button())
        {
        case Qt::LeftButton:
            if(index < getRowCount())
            {
                if(mHighlightingMode || mPermanentHighlightingMode)
                {
                    if(getColumnIndexFromX(event->x()) == 3) //click in instruction column
                    {
                        Instruction_t inst;
                        unsigned char opcode[16];
                        int opcodeSize;
                        mTraceFile->OpCode(index, opcode, &opcodeSize);
                        inst = mDisasm->DisassembleAt(opcode, opcodeSize, mTraceFile->Registers(index).regcontext.cip, 0);
                        CapstoneTokenizer::SingleToken token;
                        if(CapstoneTokenizer::TokenFromX(inst.tokens, token, event->x() - getColumnPosition(3), mFontMetrics))
                        {
                            if(CapstoneTokenizer::IsHighlightableToken(token))
                            {
                                if(!CapstoneTokenizer::TokenEquals(&token, &mHighlightToken) || event->button() == Qt::RightButton)
                                    mHighlightToken = token;
                                else
                                    mHighlightToken = CapstoneTokenizer::SingleToken();
                            }
                            else if(!mPermanentHighlightingMode)
                            {
                                mHighlightToken = CapstoneTokenizer::SingleToken();
                            }
                        }
                        else if(!mPermanentHighlightingMode)
                        {
                            mHighlightToken = CapstoneTokenizer::SingleToken();
                        }
                    }
                    else if(!mPermanentHighlightingMode)
                    {
                        mHighlightToken = CapstoneTokenizer::SingleToken();
                    }
                    if(mHighlightingMode) //disable highlighting mode after clicked
                    {
                        mHighlightingMode = false;
                        reloadData();
                    }
                }
                if(event->modifiers() & Qt::ShiftModifier)
                    expandSelectionUpTo(index);
                else
                    setSingleSelection(index);
                mHistory.addVaToHistory(index);
                updateViewport();
                return;
            }
            break;
        case Qt::MiddleButton:
            copyCipSlot();
            MessageBeep(MB_OK);
            break;
        case Qt::BackButton:
            gotoPreviousSlot();
            break;
        case Qt::ForwardButton:
            gotoNextSlot();
            break;
        }
    }
    AbstractTableView::mousePressEvent(event);
}

void TraceBrowser::mouseDoubleClickEvent(QMouseEvent* event)
{
    if(event->button() == Qt::LeftButton && mTraceFile != nullptr && mTraceFile->Progress() == 100)
    {
        switch(getColumnIndexFromX(event->x()))
        {
        case 0://Index: ???
            break;
        case 1://Address: set RVA
            if(mRvaDisplayEnabled && mTraceFile->Registers(getInitialSelection()).regcontext.cip == mRvaDisplayBase)
                mRvaDisplayEnabled = false;
            else
            {
                mRvaDisplayEnabled = true;
                mRvaDisplayBase = mTraceFile->Registers(getInitialSelection()).regcontext.cip;
            }
            reloadData();
            break;
        case 2: //Opcode: Breakpoint
            mBreakpointMenu->toggleInt3BPActionSlot();
            break;
        case 3: //Instructions: ???
            break;
        case 4: //Comment
            setCommentSlot();
            break;
        }
    }
    AbstractTableView::mouseDoubleClickEvent(event);
}

void TraceBrowser::mouseMoveEvent(QMouseEvent* event)
{
    dsint index = getIndexOffsetFromY(transY(event->y())) + getTableOffset();
    if((event->buttons() & Qt::LeftButton) != 0 && getGuiState() == AbstractTableView::NoState && mTraceFile != nullptr && mTraceFile->Progress() == 100)
    {
        if(index < getRowCount())
        {
            setSingleSelection(getInitialSelection());
            expandSelectionUpTo(index);
        }
        if(transY(event->y()) > this->height())
        {
            verticalScrollBar()->triggerAction(QAbstractSlider::SliderSingleStepAdd);
        }
        else if(transY(event->y()) < 0)
        {
            verticalScrollBar()->triggerAction(QAbstractSlider::SliderSingleStepSub);
        }
        updateViewport();
    }
    AbstractTableView::mouseMoveEvent(event);
}

void TraceBrowser::keyPressEvent(QKeyEvent* event)
{
    int key = event->key();
    int curindex = getInitialSelection();
    int visibleindex = curindex;
    if((key == Qt::Key_Up || key == Qt::Key_Down) && mTraceFile && mTraceFile->Progress() == 100)
    {
        if(key == Qt::Key_Up)
        {
            if(event->modifiers() == Qt::ShiftModifier)
            {
                if(curindex == getSelectionStart())
                {
                    if(getSelectionEnd() > 0)
                    {
                        visibleindex = getSelectionEnd() - 1;
                        expandSelectionUpTo(visibleindex);
                    }
                }
                else
                {
                    if(getSelectionStart() > 0)
                    {
                        visibleindex = getSelectionStart() - 1;
                        expandSelectionUpTo(visibleindex);
                    }
                }
            }
            else
            {
                if(curindex > 0)
                {
                    visibleindex = curindex - 1;
                    setSingleSelection(visibleindex);
                }
            }
        }
        else
        {
            if(getSelectionEnd() + 1 < mTraceFile->Length())
            {
                if(event->modifiers() == Qt::ShiftModifier)
                {
                    visibleindex = getSelectionEnd() + 1;
                    expandSelectionUpTo(visibleindex);
                }
                else
                {
                    visibleindex = getSelectionEnd() + 1;
                    setSingleSelection(visibleindex);
                }
            }
        }
        makeVisible(visibleindex);
        mHistory.addVaToHistory(visibleindex);
        updateViewport();
    }
    else
        AbstractTableView::keyPressEvent(event);
}

void TraceBrowser::tokenizerConfigUpdatedSlot()
{
    mDisasm->UpdateConfig();
    mPermanentHighlightingMode = ConfigBool("Disassembler", "PermanentHighlightingMode");
}

void TraceBrowser::expandSelectionUpTo(duint to)
{
    if(to < mSelection.firstSelectedIndex)
    {
        mSelection.fromIndex = to;
    }
    else if(to > mSelection.firstSelectedIndex)
    {
        mSelection.toIndex = to;
    }
    else if(to == mSelection.firstSelectedIndex)
    {
        setSingleSelection(to);
    }
}

void TraceBrowser::setSingleSelection(duint index)
{
    mSelection.firstSelectedIndex = index;
    mSelection.fromIndex = index;
    mSelection.toIndex = index;
}

duint TraceBrowser::getInitialSelection()
{
    return mSelection.firstSelectedIndex;
}

duint TraceBrowser::getSelectionSize()
{
    return mSelection.toIndex - mSelection.fromIndex + 1;
}

duint TraceBrowser::getSelectionStart()
{
    return mSelection.fromIndex;
}

duint TraceBrowser::getSelectionEnd()
{
    return mSelection.toIndex;
}

void TraceBrowser::makeVisible(duint index)
{
    if(index < getTableOffset())
        setTableOffset(index);
    else if(index + 2 > getTableOffset() + getViewableRowsCount())
        setTableOffset(index - getViewableRowsCount() + 2);
}

QString TraceBrowser::getIndexText(duint index)
{
    QString indexString;
    indexString = QString::number(index, 16).toUpper();
    if(mTraceFile->Length() < 16)
        return indexString;
    int digits;
    digits = floor(log2(mTraceFile->Length() - 1) / 4) + 1;
    digits -= indexString.size();
    while(digits > 0)
    {
        indexString = '0' + indexString;
        digits = digits - 1;
    }
    return indexString;
}

void TraceBrowser::updateColors()
{
    AbstractTableView::updateColors();
    //CapstoneTokenizer::UpdateColors(); //Already called in disassembly
    mDisasm->UpdateConfig();
    backgroundColor = ConfigColor("DisassemblyBackgroundColor");

    mInstructionHighlightColor = ConfigColor("InstructionHighlightColor");
    mSelectionColor = ConfigColor("DisassemblySelectionColor");
    mCipBackgroundColor = ConfigColor("DisassemblyCipBackgroundColor");
    mCipColor = ConfigColor("DisassemblyCipColor");
    mBreakpointBackgroundColor = ConfigColor("DisassemblyBreakpointBackgroundColor");
    mBreakpointColor = ConfigColor("DisassemblyBreakpointColor");
    mHardwareBreakpointBackgroundColor = ConfigColor("DisassemblyHardwareBreakpointBackgroundColor");
    mHardwareBreakpointColor = ConfigColor("DisassemblyHardwareBreakpointColor");
    mBookmarkBackgroundColor = ConfigColor("DisassemblyBookmarkBackgroundColor");
    mBookmarkColor = ConfigColor("DisassemblyBookmarkColor");
    mLabelColor = ConfigColor("DisassemblyLabelColor");
    mLabelBackgroundColor = ConfigColor("DisassemblyLabelBackgroundColor");
    mSelectedAddressBackgroundColor = ConfigColor("DisassemblySelectedAddressBackgroundColor");
    mTracedAddressBackgroundColor = ConfigColor("DisassemblyTracedBackgroundColor");
    mSelectedAddressColor = ConfigColor("DisassemblySelectedAddressColor");
    mAddressBackgroundColor = ConfigColor("DisassemblyAddressBackgroundColor");
    mAddressColor = ConfigColor("DisassemblyAddressColor");
    mBytesColor = ConfigColor("DisassemblyBytesColor");
    mBytesBackgroundColor = ConfigColor("DisassemblyBytesBackgroundColor");
    mAutoCommentColor = ConfigColor("DisassemblyAutoCommentColor");
    mAutoCommentBackgroundColor = ConfigColor("DisassemblyAutoCommentBackgroundColor");
    mCommentColor = ConfigColor("DisassemblyCommentColor");
    mCommentBackgroundColor = ConfigColor("DisassemblyCommentBackgroundColor");
}

void TraceBrowser::openFileSlot()
{
    BrowseDialog browse(this, tr("Open run trace file"), tr("Open trace file"), tr("Run trace files (*.%1);;All files (*.*)").arg(ArchValue("trace32", "trace64")), QApplication::applicationDirPath(), false);
    if(browse.exec() != QDialog::Accepted)
        return;
    emit openSlot(browse.path);
}

void TraceBrowser::openSlot(const QString & fileName)
{
    mTraceFile = new TraceFileReader(this);
    connect(mTraceFile, SIGNAL(parseFinished()), this, SLOT(parseFinishedSlot()));
    mTraceFile->Open(fileName);
}

void TraceBrowser::toggleRunTraceSlot()
{
    if(DbgValFromString("tr.runtraceenabled()") == 1)
        DbgCmdExec("StopRunTrace");
    else
    {
        QString defaultFileName;
        char moduleName[MAX_MODULE_SIZE];
        QDateTime currentTime = QDateTime::currentDateTime();
        duint defaultModule = DbgValFromString("mod.main()");
        if(DbgFunctions()->ModNameFromAddr(defaultModule, moduleName, false))
        {
            defaultFileName = QString::fromUtf8(moduleName);
        }
        defaultFileName += "-" + QLocale(QString(currentLocale)).toString(currentTime.date()) + "-" + currentTime.time().toString("hh-mm-ss") + ArchValue(".trace32", ".trace64");
        BrowseDialog browse(this, tr("Select stored file"), tr("Store run trace to the following file"),
                            tr("Run trace files (*.%1);;All files (*.*)").arg(ArchValue("trace32", "trace64")), QCoreApplication::applicationDirPath() + QDir::separator() + defaultFileName, true);
        if(browse.exec() == QDialog::Accepted)
            DbgCmdExec(QString("StartRunTrace %1").arg(browse.path).toUtf8().constData());
    }
}

void TraceBrowser::closeFileSlot()
{
    mTraceFile->Close();
    delete mTraceFile;
    mTraceFile = nullptr;
    reloadData();
}

void TraceBrowser::parseFinishedSlot()
{
    if(mTraceFile->isError())
    {
        SimpleErrorBox(this, tr("Error"), "Error when opening run trace file");
        delete mTraceFile;
        mTraceFile = nullptr;
        setRowCount(0);
    }
    else
    {
        if(mTraceFile->HashValue() && DbgIsDebugging())
            if(DbgFunctions()->DbGetHash() != mTraceFile->HashValue())
            {
                SimpleWarningBox(this, tr("Trace file is recorded for another debuggee"),
                                 tr("Checksum is different for current trace file and the debugee. This probably means you have opened a wrong trace file. This trace file is recorded for \"%1\"").arg(mTraceFile->ExePath()));
            }
        setRowCount(mTraceFile->Length());
    }
    reloadData();
}

void TraceBrowser::gotoSlot()
{
    GotoDialog gotoDlg(this, false, true); // Problem: Cannot use when not debugging
    if(gotoDlg.exec() == QDialog::Accepted)
    {
        auto val = DbgValFromString(gotoDlg.expressionText.toUtf8().constData());
        if(val > 0 && val < mTraceFile->Length())
        {
            setSingleSelection(val);
            makeVisible(val);
            mHistory.addVaToHistory(val);
            updateViewport();
        }
    }
}

void TraceBrowser::gotoNextSlot()
{
    if(mHistory.historyHasNext())
    {
        auto index = mHistory.historyNext();
        setSingleSelection(index);
        makeVisible(index);
        updateViewport();
    }
}

void TraceBrowser::gotoPreviousSlot()
{
    if(mHistory.historyHasPrev())
    {
        auto index = mHistory.historyPrev();
        setSingleSelection(index);
        makeVisible(index);
        updateViewport();
    }
}

void TraceBrowser::copyCipSlot()
{
    QString clipboard;
    for(auto i = getSelectionStart(); i <= getSelectionEnd(); i++)
    {
        if(i != getSelectionStart())
            clipboard += "\r\n";
        clipboard += ToPtrString(mTraceFile->Registers(i).regcontext.cip);
    }
    Bridge::CopyToClipboard(clipboard);
}

void TraceBrowser::copyIndexSlot()
{
    QString clipboard;
    for(auto i = getSelectionStart(); i <= getSelectionEnd(); i++)
    {
        if(i != getSelectionStart())
            clipboard += "\r\n";
        clipboard += getIndexText(i);
    }
    Bridge::CopyToClipboard(clipboard);
}


void TraceBrowser::copyDisassemblySlot()
{
    QString clipboardHtml = QString("<div style=\"font-family: %1; font-size: %2px\">").arg(font().family()).arg(getRowHeight());
    QString clipboard = "";
    for(auto i = getSelectionStart(); i <= getSelectionEnd(); i++)
    {
        if(i != getSelectionStart())
        {
            clipboard += "\r\n";
            clipboardHtml += "<br/>";
        }
        RichTextPainter::List richText;
        unsigned char opcode[16];
        int opcodeSize;
        mTraceFile->OpCode(i, opcode, &opcodeSize);
        Instruction_t inst = mDisasm->DisassembleAt(opcode, opcodeSize, mTraceFile->Registers(i).regcontext.cip, 0);
        CapstoneTokenizer::TokenToRichText(inst.tokens, richText, 0);
        RichTextPainter::htmlRichText(richText, clipboardHtml, clipboard);
    }
    clipboardHtml += QString("</div>");
    Bridge::CopyToClipboard(clipboard, clipboardHtml);
}

void TraceBrowser::setCommentSlot()
{
    if(!DbgIsDebugging())
        return;
    duint wVA = mTraceFile->Registers(getInitialSelection()).regcontext.cip;
    LineEditDialog mLineEdit(this);
    mLineEdit.setTextMaxLength(MAX_COMMENT_SIZE - 2);
    QString addr_text = ToPtrString(wVA);
    char comment_text[MAX_COMMENT_SIZE] = "";
    if(DbgGetCommentAt((duint)wVA, comment_text))
    {
        if(comment_text[0] == '\1') //automatic comment
            mLineEdit.setText(QString(comment_text + 1));
        else
            mLineEdit.setText(QString(comment_text));
    }
    mLineEdit.setWindowTitle(tr("Add comment at ") + addr_text);
    if(mLineEdit.exec() != QDialog::Accepted)
        return;
    QString comment = mLineEdit.editText.replace('\r', "").replace('\n', "");
    if(!DbgSetCommentAt(wVA, comment.toUtf8().constData()))
        SimpleErrorBox(this, tr("Error!"), tr("DbgSetCommentAt failed!"));

    static bool easter = isEaster();
    if(easter && comment.toLower() == "oep")
    {
        QFile file(":/icons/images/egg.wav");
        if(file.open(QIODevice::ReadOnly))
        {
            QByteArray egg = file.readAll();
            PlaySoundA(egg.data(), 0, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
        }
    }

    GuiUpdateAllViews();
}

void TraceBrowser::enableHighlightingModeSlot()
{
    if(mHighlightingMode)
        mHighlightingMode = false;
    else
        mHighlightingMode = true;
    reloadData();
}

void TraceBrowser::followDisassemblySlot()
{
    DbgCmdExec(QString("dis ").append(ToPtrString(mTraceFile->Registers(getInitialSelection()).regcontext.cip)).toUtf8().constData());
}

void TraceBrowser::searchConstantSlot()
{
    WordEditDialog constantDlg(this);
    constantDlg.setup(tr("Constant"), 0, sizeof(duint));
    if(constantDlg.exec() == QDialog::Accepted)
    {
        TraceFileSearchConstantRange(mTraceFile, constantDlg.getVal(), constantDlg.getVal());
        emit displayReferencesWidget();
    }
}

void TraceBrowser::searchMemRefSlot()
{
    WordEditDialog memRefDlg(this);
    memRefDlg.setup(tr("References"), 0, sizeof(duint));
    if(memRefDlg.exec() == QDialog::Accepted)
    {
        TraceFileSearchMemReference(mTraceFile, memRefDlg.getVal());
        emit displayReferencesWidget();
    }
}

void TraceBrowser::updateSlot()
{
    if(mTraceFile && mTraceFile->Progress() == 100) // && this->isVisible()
    {
        mTraceFile->purgeLastPage();
        setRowCount(mTraceFile->Length());
        reloadData();
    }
}
