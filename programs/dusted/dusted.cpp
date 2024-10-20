
// This is a little text-editor project
// It's purpose in life is to expose problems with the toolkit

// TODO:
//
//  - full state save
//  - auto-complete providers?
//
// We can now do this with "make dusted-complete":
// clang -Wno-everything -x c++ -std=c++11 \
//  -fsyntax-only -Xclang -code-completion-at -Xclang -:$line:$char -
//
// Should we auto-save all on build?
//
// We have basic file-watcher, but do we want to preserve undo?
// Also we probably want to try to preserve cursor position?

#include "dust/gui/app.h"
#include "dust/core/process.h"
#include "dust/widgets/label.h"
#include "dust/widgets/button.h"
#include "dust/widgets/scroll.h"
#include "dust/widgets/textbox.h"
#include "dust/widgets/textarea.h"
#include "dust/widgets/gridpanel.h"
#include "dust/widgets/tabs.h"
#include "dust/widgets/logview.h"

#include "dust/regex/lore.h"

#include "dusted-syntax.h"

#include <sys/stat.h>

#ifndef _WIN32
#include <dirent.h>
#include <stdlib.h>
#else
#include <direct.h>
#endif

// FIXME: figure out a sensible way to configure this
#ifndef DUSTED_DEFAULT_SCALE
#define DUSTED_DEFAULT_SCALE 100
#endif

// This is a temporary hack of a treeview..
// We really want to replace it with something better.
//
// The obvious solution would be to put the whole thing into
// a single control that handles all the logic directly.
//
// The downside with that is that it doesn't allow one to dump
// random junk into the tree.. but that's probably fine?
// 
namespace dust
{
    static ComponentManager<Font, Window>   treeFont;

    // these are unicode symbols used to prefix open/closed folders
    static const unsigned treeSymbolOpen    = 0x25BC;
    static const unsigned treeSymbolClosed  = 0x25B6;

    // This stuff needs to be made general somehow
    struct TreeViewNode : ButtonBase
    {
        std::function<void(const std::string&)> onSelect
        = [](const std::string & path) {};

        std::string     path;
        std::string     label;

        unsigned        level;

        int   sizeX, sizeY;

        TreeViewNode(const std::string & path,
            const std::string & label, unsigned level)
        : path(path), label(label), level(level)
        {
            onClick = [this](){ onSelect(this->path); };

            style.rule = LayoutStyle::NORTH;

            recalculateSize();
        }

        Font & getFont(Window * win)
        {
            // FIXME!
            Font & font = treeFont.getReference(win);
            if(!font.valid(win->getDPI()))
                font.loadDefaultFont(8, win->getDPI());
            return font;
        }

        void recalculateSize()
        {
            Window * win = getWindow();
            if(!win) return;

            Font & font = getFont(win);

            sizeX = (int) ceilf(font->getTextWidth(label)
                + font->getLineHeight() * (1 + level));
            sizeY = (int) ceilf(font->getLineHeight() + 6*win->pt());

            style.padding.north = sizeY / win->pt();
        }

        virtual ARGB getColor() { return theme.fgColor; }

        void render(dust::RenderContext & rc)
        {
            Window * win = getWindow();
            Font & font = getFont(win);

            bool down = (isMousePressed && isMouseOver);
            bool glow = (isMouseOver || isMousePressed);

            if(down) rc.clear(theme.midColor);
            else if(glow) rc.clear(theme.bgMidColor);
            else rc.clear(theme.bgColor);

            float shift = font->getLineHeight() * level;

            rc.drawText(font, label, paint::Color(getColor()),
                shift, 3*win->pt() + font->getAscent());
        }

        void ev_dpi(float dpi)
        {
            recalculateSize();
        }

        int ev_size_x(float dpi) { return sizeX; }
        int ev_size_y(float dpi) { return sizeY; }

    };

    struct TreeViewDir : TreeViewNode
    {
        std::vector<std::unique_ptr<TreeViewDir>>   subDirs;
        std::vector<std::unique_ptr<TreeViewNode>>  files;

        bool isOpen = false;

        TreeViewDir(const std::string & path,
            const std::string & name, unsigned level)
        : TreeViewNode(path, name, level)
        {
            onClick = [this](){ this->toggle(); };
        }

        ~TreeViewDir() { clear(); }

        void toggle()
        {
            if(!isOpen) readDirectory(); else clear();
        }

        void clear()
        {
            subDirs.clear();
            files.clear();
            isOpen = false;
            reflow();
        }

        virtual ARGB getColor() { return theme.actColor; }

        void render(dust::RenderContext & rc)
        {
            Window * win = getWindow();
            Font & font = getFont(win);

            bool down = (isMousePressed && isMouseOver);
            bool glow = (isMouseOver || isMousePressed);

            if(down) rc.clear(theme.midColor);
            else if(glow) rc.clear(theme.bgMidColor);
            else rc.clear(theme.bgColor);

            float baseShift = font->getLineHeight() * level;
            float shift = font->getLineHeight() * (level + 1);

            float y = 3*win->pt() + font->getAscent();

            unsigned sym = isOpen ? treeSymbolOpen : treeSymbolClosed;

            // NOTE: shifting up by descent works with our default font
            // not sure if this holds for other fonts though
            rc.drawChar(font, sym, paint::Color(getColor()), baseShift,
                y - .5f * font->getDescent());

            rc.drawText(font, label, paint::Color(getColor()), shift, y);
        }

        void readDirectory()
        {
            clear();
            isOpen = true;
            
#ifdef _WIN32
            WIN32_FIND_DATA data;
            HANDLE hFind = FindFirstFileW(to_u16(path+"\\*").c_str(), &data);
            
            if ( hFind != INVALID_HANDLE_VALUE ) {
                do {
                    auto filename = to_u8(data.cFileName);
                    
                    if(filename[0] == '.') continue;
                    // ignore the temp files textarea sometimes leaves behind
                    if(strstr(filename.c_str(), ".$tmp")) continue;
                
                    std::string newPath = path + '\\' + filename;

                    if(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    {
                        subDirs.emplace_back(
                            new TreeViewDir(newPath, filename, level + 1));
                    }
                    else
                    {
                        files.emplace_back(
                            new TreeViewNode(newPath, filename, level + 1));
                    }
                } while (FindNextFileW(hFind, &data));
                FindClose(hFind);
            }
#else
            DIR * dir = opendir(path.c_str());
            if(!dir) return;

            while(true)
            {
                struct dirent * entry = readdir(dir);
                if(!entry) break;

                // skip .files for now
                if(entry->d_name[0] == '.') continue;

                // get name
                std::string name(entry->d_name, entry->d_namlen);
                std::string newPath = path + '/' + name;

                bool isDir = entry->d_type == DT_DIR;

                // if this is a symbolic link, we need to figure out
                // if the target path is a file or a directory
                if(entry->d_type == DT_LNK || entry->d_type == DT_UNKNOWN)
                {
                    struct stat statBuf;
                    stat(newPath.c_str(), &statBuf);
                    if(statBuf.st_mode & S_IFDIR) isDir = true;
                }

                if(isDir)
                {
                    subDirs.emplace_back(new TreeViewDir(newPath, name, level + 1));
                }
                else
                {
                    files.emplace_back(new TreeViewNode(newPath, name, level + 1));
                }
            }
            
            closedir(dir);
#endif
            auto compareDirs = [](
                std::unique_ptr<TreeViewDir> const & a,
                std::unique_ptr<TreeViewDir> const & b)
            {
                return a->label < b->label;
            };

            std::sort(subDirs.begin(), subDirs.end(), compareDirs);
            for(auto & p : subDirs)
            {
                p->setParent(this);
                p->onSelect = onSelect;
            }

            auto compareFiles = [](
                std::unique_ptr<TreeViewNode> const & a,
                std::unique_ptr<TreeViewNode> const & b)
            {
                return a->label < b->label;
            };

            std::sort(files.begin(), files.end(), compareFiles);
            for(auto & p : files)
            {
                p->setParent(this);
                p->onSelect = onSelect;
            }
        }
    };
};

struct FileBrowser : dust::Panel
{
    dust::Button        btnChdir;
    dust::Label         lblChdir;
    
    dust::ScrollPanel   scroll;
    dust::TreeViewDir   root;

    struct Filler : Panel
    {
        void render(dust::RenderContext & rc)
        {
            rc.clear(dust::theme.bgColor);
        }
    } filler;

    FileBrowser()
    : root(".", "<Files>", 1)
    {
        style.rule = dust::LayoutStyle::FILL;

        btnChdir.setParent(*this);
        btnChdir.style.rule = dust::LayoutStyle::SOUTH;
        lblChdir.setParent(btnChdir);
        lblChdir.setText("Change project..");

        scroll.setParent(*this);
        // lblChdir forces a minimum width
        root.setParent(scroll.getContent());

        filler.style.rule = dust::LayoutStyle::FILL;
        filler.setParent(scroll.getContent());

        updateRoot();
    }
    
    void updateRoot()
    {
#ifdef _WIN32
        wchar_t rootPath[MAX_PATH+1];
        const wchar_t * cwd = _wfullpath(rootPath, to_u16(root.path).c_str(), MAX_PATH);
        auto * basename = wcsrchr(cwd, '\\');
        if(basename) root.label = to_u8(basename + 1);
        root.path = to_u8(cwd);
#else
        char rootPath[PATH_MAX];
        const char * cwd = realpath(root.path.c_str(), rootPath);
        auto * basename = strrchr(cwd, '/');
        if(basename) root.label = basename + 1;
        root.path = cwd;
#endif
        root.clear();

        // set current working directory
#ifdef _WIN32
        _wchdir(cwd);
#else
        chdir(cwd);
#endif
    }
};

// returns the mtime for a file, or 0 if there is a problem
static time_t getTimeForPath(const std::string & path)
{
    if(!path.size()) return 0;

#ifdef _WIN32
    struct _stat64 statBuf;
    if(_wstat64(to_u16(path).c_str(), &statBuf)) return 0; // error
    
    return statBuf.st_mtime;
#else
    struct stat statBuf;
    if(stat(path.c_str(), &statBuf)) return 0; // error

    // NOTE: on macOS we have st_mtimespec
    // on other POSIX systems it's st_mtime
    return statBuf.st_mtimespec.tv_sec;
#endif
}

// we might want to put several of these side by side?
struct Document : dust::Panel
{
    dust::ScrollPanel   scroll;
    dust::TextArea      editor;

    std::string         path;

    // last modification time for the file
    // used to avoid accidental loss of data
    time_t              mtimeFile;

    // used to notify appwindow to redraw tab strips
    dust::Notify        onSaveAs;
    dust::Notify        onCompletion;

    // used to notify appwindow about context menu actions
    dust::Notify        onMenuNew;
    dust::Notify        onMenuOpen;

    Document()
    {
        style.rule = dust::LayoutStyle::FILL;

        scroll.setParent(this);
        scroll.setOverscroll(0, .5f);

        editor.setParent(scroll.getContent());

        editor.onContextMenu = [this](MouseEvent const & ev)
        {
            enum
            {
                idCut, idCopy, idPaste,
                idNew, idOpen, idSave, idSaveAs
            };
            auto onSelect = [this](unsigned id)
            {
                switch(id)
                {
                case idCut: editor.doCut(); break;
                case idCopy: editor.doCopy(); break;
                case idPaste: editor.doPaste(); break;

                case idNew: onMenuNew(); break;
                case idOpen: onMenuOpen(); break;
                
                case idSave: doSave(false, dust::doNothing); break;
                case idSaveAs: doSave(true, dust::doNothing); break;
                }
            };
    
            auto * menu = getWindow()->createMenu(onSelect);
            menu->addItem("Cut", idCut);
            menu->addItem("Copy", idCopy);
            menu->addItem("Paste", idPaste);
            menu->addSeparator();
            menu->addItem("New", idNew);
            menu->addItem("Open...", idOpen);
            menu->addItem("Save", idSave);
            menu->addItem("Save As...", idSaveAs);
            menu->activate(
                ev.x + editor.getLayout().windowOffsetX,
                ev.y + editor.getLayout().windowOffsetY);
        };
    }

    void selectSyntax()
    {
        dust::SyntaxParser * sp = 0;
        
        if(!sp && SyntaxC::wantFileType(path)) sp = new SyntaxC;
        if(!sp && SyntaxScript::wantFileType(path)) sp = new SyntaxScript;
        
        editor.syntaxParser.reset(sp);
    }

    void doSave(bool saveAs, dust::Notify onDone)
    {
        if(!path.size()) saveAs = true;

        if(saveAs)
        {
            // get a filename using system dialog and try again
            auto save = [this, onDone](){
                onSaveAs();
                doSave(false, onDone);
#ifdef _WIN32
                wchar_t absPath[MAX_PATH+1];
                _wfullpath(absPath, to_u16(path).c_str(), MAX_PATH);
                path = to_u8(absPath);
#else
                char absPath[PATH_MAX];
                path = realpath(path.c_str(), absPath);
#endif
                selectSyntax();
                editor.recalculateSize();   // force syntax recolor
            };
            getWindow()->saveAsDialog(path, save, dust::doNothing); 
        }
        else
        {
            editor.saveFile(path);
            editor.onUpdate();
            mtimeFile = getTimeForPath(path);
            onDone();
        }
    }

    bool ev_key(dust::Scancode vk, bool pressed, unsigned mods)
    {
        if(!pressed) return false;

        if(mods & dust::KEYMOD_CMD)
        switch(vk)
        {
            case dust::SCANCODE_S:
                doSave(mods & dust::KEYMOD_SHIFT, dust::doNothing);
                break;
            case dust::SCANCODE_TAB: onCompletion(); break;
            default: return false;
        }

        return false;
    }
};

#include "dusted-icon.h"

struct TestIcon : dust::SVG
{
    TestIcon()
    {
        load(testSVG);
    }
} testIcon;

struct NoDocument : dust::Panel
{
    struct Background : dust::ButtonBase
    {
        Background()
        {
            // not doing hovers, so save CPU
            trackHover = false;
        }
        
        void render(dust::RenderContext & rc)
        {
            rc.clear(dust::theme.bgColor);

            testIcon.renderFit(rc, layout.w, layout.h);
        }
    } background;

    // make size match actual documents
    dust::ScrollPanel    scroll;

    NoDocument()
    {
        style.rule = dust::LayoutStyle::FILL;

        scroll.setParent(this);
        background.setParent(scroll.getContent());
        background.style.rule = dust::LayoutStyle::FILL;
    }
};

typedef dust::TabPanel<Document, NoDocument> DocumentPanel;
struct DocumentPanelEx : DocumentPanel
{
    std::function<void(const char*)>    onDropFile;

    DocumentPanelEx()
    {
        hoverFiles.setVisible(false);
        hoverFiles.setParent(this);
    }
    
    bool ev_mouse(const MouseEvent & ev)
    {
        if(ev.type == dust::MouseEvent::tDragFiles)
        {
            if(!hoverFiles.getVisible())
                hoverFiles.setVisible(true);
            
            return true;
        }

        return DocumentPanel::ev_mouse(ev);
    }

    void ev_mouse_exit()
    {
        if(hoverFiles.getVisible())
            hoverFiles.setVisible(false);
        DocumentPanel::ev_mouse_exit();
    }

    bool ev_accept_files() { return true; }

    void ev_drop_file(const char * filename) { onDropFile(filename); }
  
private:
    struct Overlay : dust::Panel
    {
        Overlay() { style.rule = dust::LayoutStyle::OVERLAY; }
        void render(dust::RenderContext & rc)
        {
            auto pt = getWindow()->pt();
            Path p;
            p.rect(3*pt, 3*pt, layout.w-3*pt, layout.h-3*pt, 3*pt);
            rc.strokePath(p, 1.5f*pt, dust::paint::Color(dust::theme.actColor));
        }
    } hoverFiles;
};

typedef DocumentPanel::Tab  DocumentTab;

struct FindPanel : dust::Grid<2,2>
{
    dust::Panel         findGroup;
    dust::Label         findLabel;
    dust::TextButton    findNextButton;
    dust::TextButton    findPrevButton;
    dust::TextBox       findBox;

    dust::Panel         replaceGroup;
    dust::Label         replaceLabel;
    dust::TextButton    replaceButton;
    dust::TextButton    replaceAllButton;
    dust::TextBox       replaceBox;

    dust::Label         findStatus;

    FindPanel()
    {
        style.rule = dust::LayoutStyle::SOUTH;

        Font monofont;
        monofont.loadDefaultFont(8.f, 72.f, true);

        weightColumn(0, 1);

        insert(1, 0, findGroup);

        findNextButton.setParent(findGroup);
        findNextButton.style.rule = dust::LayoutStyle::EAST;
        findNextButton.label.style.rule = dust::LayoutStyle::FILL;
        findNextButton.label.font = monofont;
        findNextButton.label.setText("\u25B6");

        findPrevButton.setParent(findGroup);
        findPrevButton.style.rule = dust::LayoutStyle::EAST;
        findPrevButton.label.style.rule = dust::LayoutStyle::FILL;
        findPrevButton.label.font = monofont;
        findPrevButton.label.setText("\u25C0");
        
        findLabel.setParent(findGroup);
        findLabel.setText("Search:");
        
        insert(0, 0, findBox);
        findBox.style.padding.east = 6;
        findBox.onResetColor = [this]() { findStatus.setText(""); };

        insert(1, 1, replaceGroup);
        
        replaceAllButton.setParent(replaceGroup);
        replaceAllButton.style.rule = dust::LayoutStyle::EAST;
        replaceAllButton.label.style.rule = dust::LayoutStyle::FILL;
        replaceAllButton.label.font = monofont;
        replaceAllButton.label.setText("\u2200");
        
        replaceButton.setParent(replaceGroup);
        replaceButton.style.rule = dust::LayoutStyle::EAST;
        replaceButton.label.style.rule = dust::LayoutStyle::FILL;
        replaceButton.label.font = monofont;
        replaceButton.label.setText("1");
        
        replaceLabel.setParent(replaceGroup);
        replaceLabel.setText("Replace:");

        insert(0, 1, replaceBox);
        replaceBox.style.padding.east = 6;
        replaceBox.onResetColor = findBox.onResetColor;

    }
};

struct BuildScrollPanel : dust::ScrollPanel
{
    // make build-panel take 1/3 the window
    int ev_size_y(float dpi)
    {
        return getWindow()->getLayout().h / 3;
    }
};

// this just prevents the status labels from doing full redraw
struct HeaderPanel : dust::Panel
{
    void reflowChildren() { layoutAsRoot(getWindow()->getDPI()); redraw(); }
};

struct BuildPanel : dust::Panel
{
    BuildScrollPanel    scroll;

    dust::LogView       output;

    std::vector<char>   buffer;

    dust::SlaveProcess  slave;
    
    dust::Grid<2,1>     headerGrid;
    HeaderPanel         header;
    dust::Button        buildButton;
    dust::Label         buildButtonLabel;
    dust::Label         status;

    dust::TextButton    commandButton;
    dust::TextBox       commandBox;
    
    bool                buildActive = false;
    
    bool                autoClose   = false;
    unsigned            autoCloseMs = 0;
    
    BuildPanel()
    {
        style.rule = dust::LayoutStyle::SOUTH;

        headerGrid.setParent(this);
        headerGrid.style.rule = dust::LayoutStyle::NORTH;
        
        headerGrid.insert(0,0, header);
        headerGrid.weightColumn(0, 1.f);
        headerGrid.weightColumn(1, 1.f);
        headerGrid.setIgnoreContentSize(true);
        
        header.style.rule = dust::LayoutStyle::NORTH;
        
        buildButton.setParent(header);
        buildButton.style.rule = dust::LayoutStyle::WEST;
        buildButton.onClick = [this](){ doBuild(); };

        commandButton.label.setText("Run");
        commandButton.style.rule = dust::LayoutStyle::EAST;
        commandButton.onClick = [this](){ doCommand(); };
        
        commandBox.style.rule = dust::LayoutStyle::FILL;
        commandBox.onEnter = [this](){ doCommand(); };

        headerGrid.insert(1,0, commandButton);
        headerGrid.insert(1,0, commandBox);
        
        buildButtonLabel.setParent(buildButton);
        buildButtonLabel.font.loadDefaultFont(7.f, 72.f, true);
        buildButtonLabel.setText("make");
        
        status.setParent(header);
        status.style.rule = dust::LayoutStyle::WEST;
        status.setText("");
        
        scroll.setParent(this);
        scroll.style.rule = dust::LayoutStyle::FILL;
        scroll.style.minSizeY = 0;
        scroll.style.padding.west = 1;
        scroll.setEnabled(false);

        output.setParent(scroll.getContent());
    }

    void doCommand()
    {
        if(slave.isAlive()) { return; } // don't kill a build

        slave.args.clear();
        
#ifdef _WIN32
        slave.pushArg("cmd");
        slave.pushArg("/C");
#else
        slave.pushArg("/bin/sh");
        slave.pushArg("-c");
#endif
        std::vector<char>   cmd;
        commandBox.outputContents(cmd);
        cmd.push_back(0);
        slave.pushArg(cmd.data());

        runCommand("Command running...");
    }

    void doBuild()
    {
        // if we already have a build, kill it and return
        if(slave.isAlive())  { slave.kill(); return; }
        
        // setup command
        slave.args.clear();
        slave.pushArg("make");
        slave.pushArg("-kj4");    // keep going + parallel build

        runCommand("Building...");
    }

    void doCompletion(DocumentTab * tab)
    {
        if(slave.isAlive()) { return; } // don't kill a build

        slave.args.clear();
        slave.pushArg("make");
        slave.pushArg("dusted-complete");
        slave.pushArg(dust::strf("DUSTED_PATH=\"%s\"", tab->content.path.c_str()));
        slave.pushArg(dust::strf("DUSTED_LINE=%d", tab->content.editor.getCursorLine()));
        slave.pushArg(dust::strf("DUSTED_COL=%d", tab->content.editor.getCursorColumn()));

        output.clear();
        output.stopScroll();    // don't scroll for completion
        buffer.clear();
        status.setText("");
        output.bgColor = theme.bgColor;
        
        std::vector<char> txt;
        tab->content.editor.outputContents(txt);
        
        slave.start();
        slave.sendInput(txt.data(), txt.size());
        slave.closeInput();

        scroll.setEnabled(true);
        autoClose = false;
    }

    void runCommand(const char * statusTxt)
    {
        // clear data
        output.clear();
        buffer.clear();

        // start slave, we can close input immediately
        slave.start();
        slave.closeInput();
        
        buildActive = true;
        status.setText(statusTxt);
        status.color = dust::theme.warnColor;
        output.bgColor = theme.bgColor;
        
        buildButtonLabel.setText("kill");
        scroll.setEnabled(true);
        autoClose = false;
    }

    bool ev_mouse(const dust::MouseEvent & e)
    {
        // for now we support the most dumb logic ever:
        // click to show output, right-click to hide
        //
        // FIXME: implement a toggle-button instead
        if(e.type == dust::MouseEvent::tDown)
        {
            //if(e.button == 1) scroll.setEnabled(true);
            //if(e.button == 2) scroll.setEnabled(false);
            if(e.button == 1) scroll.setEnabled(!scroll.getEnabled());
            return true;
        }
        return false;
    }
    
    void ev_update()
    {
        // update slave, check the status
        bool alive = slave.update(buffer);
        
        if(buffer.size())
        {
            // update output
            output.append(buffer.data(), buffer.size());
            buffer.clear();
        }
        
        if(!alive)
        {
            // check if current build just finished
            if(buildActive)
            {
                bool error = (0 != slave.exitStatus);
                // hide panel when build was good
                bool isBuild = (slave.args[0] == "make");
                const char * txtFailure =
                    (isBuild ? "Build failed!" : "Command failed!");
                const char * txtSuccess =
                    (isBuild ? "Build finished." : "Command finished.");
                status.setText(error ? txtFailure : txtSuccess);
                status.color = (error ? dust::theme.errColor : dust::theme.goodColor);
                output.bgColor = dust::color::lerp(theme.winColor, status.color, 0x18);
                buildButtonLabel.setText("make");
                buildActive = false;
                
                if(!error && isBuild)
                {
                    autoClose = true;
                    autoCloseMs = dust::getTimeMs();
                }
            }
            
            // handle auto closing
            // use wrap-around safe comparison here
            if(autoClose && dust::getTimeMs() - autoCloseMs > 2000)
            {
                scroll.setEnabled(false);
                status.setText("");
                autoClose = false;
            }
        }
    }
};

struct AppWindow : dust::Panel
{
    dust::Grid<2,1> topGrid;
    dust::Grid<2,2> panelGrid;

    FileBrowser     browser;
    FindPanel       findPanel;
    BuildPanel      buildPanel;
    
    DocumentPanelEx panel0, panel1;
    DocumentTab     *activeTab = 0;

    void doSearch(bool replace, bool shift)
    {
        if(!activeTab) return;
    
        std::vector<char>   findStr;
        findPanel.findBox.outputContents(findStr);
        lore::Regex re(findStr.data(), findStr.size());

        if(re.error())
        {
            findPanel.findStatus.setText(
                dust::strf("Invalid pattern: %s", re.error()));
            findPanel.findBox.focus();
            findPanel.findBox.cursorColor = dust::theme.errColor;
            return;
        }

        char * repPtr = 0;
        std::vector<char> repStr;
        
        if(replace)
        {
            findPanel.replaceBox.outputContents(repStr);
            repStr.push_back(0);    // c-string api for now
            repPtr = repStr.data();
        }

        if(replace && shift)
        {
            unsigned nMatch =
                activeTab->content.editor.doReplaceAll(re, repPtr);
                
            if(nMatch)
            {
                findPanel.findStatus.setText(dust::strf("replaced %d match%s",
                    nMatch, nMatch == 1 ? "" : "es"));
            }
            else
            {
                findPanel.findStatus.setText("no results");
            }

            findPanel.findBox.cursorColor = nMatch
                ? dust::theme.goodColor : dust::theme.warnColor;
        }
        else
        {
            bool backwards = shift;
            unsigned index = 0;
            unsigned nMatch = activeTab->content.editor.doSearch(
                re, backwards, index, repPtr);
    
            if(nMatch)
            {
                findPanel.findStatus.setText(dust::strf("%d/%d result%s",
                    index+1, nMatch, nMatch == 1 ? "" : "s"));
            }
            else
            {
                findPanel.findStatus.setText("no results");
            }
    
            findPanel.findBox.cursorColor = nMatch
                ? dust::theme.goodColor : dust::theme.warnColor;
        }
        
        findPanel.findBox.redraw();
        findPanel.replaceBox.cursorColor = findPanel.findBox.cursorColor;
        findPanel.replaceBox.redraw();
    }

    void forceCloseTab(DocumentTab * tab)
    {
        // close this document if it's inside panel0
        if(panel0.contains(tab)) { panel0.closeTab(tab); }
        
        // close this document if it's inside panel1
        if(panel1.contains(tab)) { panel1.closeTab(tab); }

        if(tab == activeTab) activeTab = 0;
    }

    void closeTab(DocumentTab * tab)
    {
        // if modified, ask for a dialog
        if(tab->content.editor.isModified())
        {
            auto doClose = [this, tab]() { forceCloseTab(tab); };
            auto doSave = [tab, doClose]()
            {
                tab->content.doSave(false, doClose);
            };

            getWindow()->confirmClose(doSave, doClose, dust::doNothing);
        }
        else
        {
            forceCloseTab(tab);
        }
    }

    void setWindowTitle()
    {
        if(!activeTab)
        {
            getWindow()->setTitle(browser.root.label.c_str());
            return;
        }

        const char * path = activeTab->content.path.c_str();
        if(!*path)
        {
            getWindow()->setTitle(browser.root.label.c_str());
            return;
        }
        
        const char * cwd = browser.root.path.c_str();
        
        // strip common prefix
        while(*path && *cwd && *path == *cwd) { ++path; ++cwd; }

        // if we couldn't strip the whole prefix, revert to full path
        if(*cwd)
            getWindow()->setTitle(activeTab->content.path.c_str());
        else
            getWindow()->setTitle((browser.root.label + path).c_str());
    }

    // create a new document in active panel
    void newDocument()
    {
        newDocument(panel1.contains(activeTab) ? panel1 : panel0);
    }

    // create a new document in specific panel
    // we use explicit target panels with the "no document" placeholders
    void newDocument(DocumentPanel & targetPanel)
    {
        // here we do a little special case:
        // if we have an untitled, unmodified document in current panel
        // then don't actually bother opening a new one
        if(targetPanel.getActiveTab()
        && !targetPanel.getActiveTab()->content.path.size()
        && !targetPanel.getActiveTab()->content.editor.isModified())
        {
            // redraw the strip, just in case we change the filename
            targetPanel.redrawStrip();
            return;
        }

        DocumentTab * tab = targetPanel.newTab("<untitled>");
        tab->onClose = [this, tab]()
        { this->activeTab = 0; this->closeTab(tab); setWindowTitle(); };

        tab->content.editor.onFocus = [this, tab]() {
            this->activeTab = tab;
            
            panel0.actColor = (panel0.contains(tab)) ? dust::theme.actColor : 0;
            panel0.redrawStrip();
            panel1.actColor = (panel1.contains(tab)) ? dust::theme.actColor : 0;
            panel1.redrawStrip();
            
            // check file modification time
            time_t mTime = getTimeForPath(tab->content.path);
            if(mTime > tab->content.mtimeFile)
            {
                // if file is modified locally, rename tab
                // and clear path to force user to respond
                if(tab->content.editor.isModified())
                {
                    tab->content.path.clear();
                    tab->label = "<" + tab->label + ">";
                    tab->content.mtimeFile = 0;

                    if(panel0.contains(tab)) panel0.redrawStrip();
                    if(panel1.contains(tab)) panel1.redrawStrip();
                }
                else
                {
                    tab->content.mtimeFile = mTime;
                    tab->content.editor.loadFile(tab->content.path);
                }
            }

            setWindowTitle();
        };
        tab->onSelect = [tab](){ tab->content.editor.focus(); };
        tab->content.onSaveAs = [this,tab](){
            this->setLabelFromPath(tab);
            if(panel0.contains(tab)) panel0.redrawStrip();
            if(panel1.contains(tab)) panel1.redrawStrip();
            setWindowTitle();
        };

        tab->content.onMenuNew = [this](){ newDocument(); };
        tab->content.onMenuOpen = [this](){ openDialog(); };

        tab->content.editor.focus();
        tab->content.editor.onUpdate = [this, tab]() {
            if(tab->modified != tab->content.editor.isModified())
            {
                tab->modified = tab->content.editor.isModified();
                if(panel0.contains(tab)) panel0.redrawStrip();
                if(panel1.contains(tab)) panel1.redrawStrip();
            }
        };

        tab->content.onCompletion = [this, tab]()
        {
            buildPanel.doCompletion(tab);
        };
    }

    // returns true if we found and existing document
    bool selectExisting(DocumentPanel & panel, const std::string & path)
    {
        for(int i = 0;; ++i)
        {
            DocumentTab * tab = panel.getTabByIndex(i);
            if(!tab) return false;

            if(tab->content.path == path)
            {
                panel.selectTab(i);
                tab->content.editor.focus();
                return true;
            }
        }
    }

    void setLabelFromPath(DocumentTab * tab)
    {
        tab->label = tab->content.path.substr
            (1 + tab->content.path.find_last_of("\\/"));
        setWindowTitle();
    }

    void openDialog()
    {
        auto onOpen = [&](const char * path)
        {
            openDocument(path);
        };
        getWindow()->openDialog(onOpen, true, browser.root.path.c_str());
    }

    void openDocument(const std::string & path, DocumentPanel * inPanel = 0)
    {
#ifdef _WIN32
        wchar_t w_absPath[MAX_PATH+1];
        _wfullpath(w_absPath, to_u16(path).c_str(), MAX_PATH);
        auto absPath = to_u8(w_absPath);
#else
        char absPath[PATH_MAX];
        realpath(path.c_str(), absPath);
#endif
    
        // try to find an existing document with the same path
        if(selectExisting(panel0, absPath))
        {
            if(inPanel && &panel0 != inPanel) panel0.moveTabToPanel(inPanel);
            return;
        }
        if(selectExisting(panel1, absPath))
        {
            if(inPanel && &panel1 != inPanel) panel1.moveTabToPanel(inPanel);
            return;
        }

        // if we didn't find one, open a new one in desired or active panel
        if(inPanel) newDocument(*inPanel); else newDocument();
        activeTab->content.path = absPath;
        activeTab->content.selectSyntax();
        activeTab->content.editor.loadFile(absPath);
        activeTab->content.mtimeFile = getTimeForPath(absPath);

        setLabelFromPath(activeTab);
    }

    void changeDirectory()
    {
        auto onOpenDir = [&](const char * path)
        {
            browser.root.path = path;
            browser.updateRoot();
            setWindowTitle();
        };
        getWindow()->openDirDialog(onOpenDir);
    }

    void ev_update()
    {
        // if we have no document with focus, set it to main window
        // this allows shortcuts to work
        if(!getWindow()->getFocus()) focus();
    }

    bool ev_key(dust::Scancode vk, bool pressed, unsigned mods)
    {
        if(!pressed) return false;

        if(!mods && vk == dust::SCANCODE_ESCAPE) buildPanel.scroll.setEnabled(false);

        if(mods == dust::KEYMOD_CMD)
        switch(vk)
        {
            case dust::SCANCODE_N: newDocument(); break;
            case dust::SCANCODE_O: openDialog(); break;
            case dust::SCANCODE_F:
                findPanel.findBox.focusSelectAll();
                findPanel.findStatus.setText("");
                break;

            // FIXME: add something useful to cmd-r/t/d
            // cmd-d: "describe symbol" could be useful? (currently select parens)
            
            case dust::SCANCODE_W: if(activeTab) activeTab->onClose(); break;
            case dust::SCANCODE_B: buildPanel.doBuild(); break;

            case dust::SCANCODE_COMMA:
            {
                auto & panel = (panel0.contains(activeTab)) ? panel0 : panel1;
                panel.selectPrevTab();
            }
            break;
            case dust::SCANCODE_PERIOD:
            {
                auto & panel = (panel0.contains(activeTab)) ? panel0 : panel1;
                panel.selectNextTab();
            }
            break;
            case dust::SCANCODE_SLASH:
            {
                auto & panel = (panel0.contains(activeTab)) ? panel0 : panel1;
                if(!panel.dragLink->getActiveTab()) newDocument(*panel.dragLink);
                else panel.dragLink->getActiveTab()->onSelect();
            }
            break;
            
            default: return false;
        }
        
        if(mods == (dust::KEYMOD_CMD|dust::KEYMOD_SHIFT))
        switch(vk)
        {
            case dust::SCANCODE_B: buildPanel.commandBox.focus(); break;

            case dust::SCANCODE_O: changeDirectory(); break;
            
            case dust::SCANCODE_SLASH:
            {
                auto & panel = (panel0.contains(activeTab)) ? panel0 : panel1;
                panel.moveTabToPanel(panel.dragLink);
            }
            break;
            case dust::SCANCODE_COMMA:
            {
                auto & panel = (panel0.contains(activeTab)) ? panel0 : panel1;
                panel.moveTabLeft();
            }
            break;
            case dust::SCANCODE_PERIOD:
            {
                auto & panel = (panel0.contains(activeTab)) ? panel0 : panel1;
                panel.moveTabRight();
            }
            break;
            
            default: return false;
        }

        return true;
    }

    void ev_layout(float dpi)
    {
        // switch between vertical / horizontal panel split
        // depending on whether the window is wider or taller
        bool hstack = layout.h > layout.w;
        if(hstack && panel1.getParent() == panelGrid.getCell(1,0))
        {
            panelGrid.insert(0, 1, panel1);
            panelGrid.weightRow(1, 1);
            panelGrid.weightColumn(1, 0);
            
            layoutAsRoot(dpi);
        }
        if(!hstack && panel1.getParent() == panelGrid.getCell(0,1))
        {
            panelGrid.insert(1, 0, panel1);
            panelGrid.weightRow(1, 0);
            panelGrid.weightColumn(1, 1);
            
            layoutAsRoot(dpi);
        }
    }
    

    AppWindow()
    {
        style.rule = dust::LayoutStyle::FILL;

        topGrid.setParent(this);
        topGrid.weightRow(0, 1.f);
        topGrid.weightColumn(0, 1.f);
        topGrid.weightColumn(1, 16.f);
        
        topGrid.insert(0, 0, browser);
        browser.root.onSelect = [this](const std::string & path)
        {
            this->openDocument(path);
        };

        browser.btnChdir.onClick = [&](){ changeDirectory(); };
        
        topGrid.insert(1, 0, buildPanel);
        buildPanel.output.onClickError = [this](const char *path, int l, int c)
        {
            this->openDocument(path);
            // FIXME: should check if we managed to open the file
            // and close the dummy tab if not
            this->activeTab->content.editor.focus();
            // exposePoint() is funky if we haven't done layout
            // this happens if we have to open the document
            this->layoutAsRoot(getWindow()->getDPI());
            this->activeTab->content.editor.setPosition(l, c);
        };

        auto focusActive = [this]()
        {
            if(this->activeTab) this->activeTab->content.editor.focus();
        };
        
        buildPanel.header.style.padding.east = 6;
        buildPanel.commandBox.onEscape = focusActive;
        
        findPanel.findStatus.setParent(buildPanel.header);
        findPanel.findStatus.style.rule = dust::LayoutStyle::EAST;
        
        topGrid.insert(1, 0, findPanel);
        findPanel.findBox.onEnter = [this](){ this->doSearch(false, false); };
        findPanel.findBox.onShiftEnter = [this](){ this->doSearch(false, true); };
        findPanel.findBox.onEscape = focusActive;
        findPanel.findBox.onTab = [this]()
        { findPanel.replaceBox.focusSelectAll(); };
        findPanel.findNextButton.onClick = findPanel.findBox.onEnter;
        findPanel.findPrevButton.onClick = findPanel.findBox.onShiftEnter;

        findPanel.replaceBox.onEnter = [this](){ this->doSearch(true, false); };
        findPanel.replaceBox.onShiftEnter = [this](){ this->doSearch(true, true); };
        findPanel.replaceBox.onEscape = focusActive;
        findPanel.replaceButton.onClick = findPanel.replaceBox.onEnter;
        findPanel.replaceAllButton.onClick = findPanel.replaceBox.onShiftEnter;
        findPanel.replaceBox.onTab = [this]()
        { findPanel.findBox.focusSelectAll(); };
        
        panelGrid.insert(0, 0, panel0);
        panelGrid.insert(1, 0, panel1);

        panelGrid.weightRow(0, 1);
        panelGrid.weightRow(1, 0);
        panelGrid.weightColumn(0, 1);
        panelGrid.weightColumn(1, 1);

        topGrid.insert(1, 0, panelGrid);

        // build a circular linked list for inter-panel tab dragging
        panel0.dragLink = &panel1;
        panel1.dragLink = &panel0;

        panel0.noContent.background.onClick = [this](){ newDocument(panel0); };
        panel1.noContent.background.onClick = [this](){ newDocument(panel1); };

        panel0.onDropFile = [this](const char * path)
            { openDocument(path, &panel0); };
        panel1.onDropFile = [this](const char * path)
            { openDocument(path, &panel1); };
    }
};

struct Dusted : dust::Application
{
    AppWindow   appWin;

    void app_startup()
    {
        dust::Window * win = dust::createWindow(*this, 0, 16*72, 9*72);
        win->setMinSize(16*32, 9*32);
        win->setScale(DUSTED_DEFAULT_SCALE);
        win->toggleMaximize();

#ifdef _WIN32
        int iconsize = GetSystemMetrics(SM_CXICON);
#endif
#ifdef __APPLE__
        int iconsize = 128; // macos scales fine, so can make this "large"
#endif
        dust::Surface sIcon(iconsize, iconsize);
        dust::RenderContext rcIcon(sIcon);
        rcIcon.clear();

        dust::RenderContext rcIconOff(rcIcon, iconsize/16, iconsize/16);
        testIcon.renderFit(rcIconOff, iconsize-iconsize/8, iconsize-iconsize/8);

        // blur background
        dust::Surface sIcon2(iconsize, iconsize);
        dust::RenderContext rcIcon2(sIcon2);
        sIcon2.blur(sIcon, iconsize/32.f);
        rcIcon2.fill<dust::blend::InnerShadow>(dust::paint::Color(0xffff4488));
        rcIcon.copy<dust::blend::Under>(sIcon2);
        sIcon2.blur(sIcon, iconsize/32.f);
        rcIcon2.fill<dust::blend::InnerShadow>(dust::paint::Color(0xffdd8888));
        rcIcon.copy<dust::blend::Under>(sIcon2);
        
        win->setIcon(sIcon);

        appWin.setParent(win);
        appWin.setWindowTitle();
    }

    bool win_closing()
    {
        // FIXME: check if we have any unsaved documents
        // then prompt to save those, or return false if user cancels
        return true;
    }
};

#ifdef _WIN32
int WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR args, int nShowCmd)
#else
int main()
#endif
{
    Dusted app;

    app.run();
}
