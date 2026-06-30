#define PCRE2_CODE_UNIT_WIDTH 16
#include <pcre2.h>

#include <QString>
#include <QAction>
#include <QMenu>
#include <QToolButton>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QCompleter>
#include <QRegularExpression>
#include <QDebug>

#include "Dialogs/DryRunReplace.h"
#include "Dialogs/ReplacementPreviewPlus.h"
#include "Tabs/TextTab.h"
#include "Tabs/FlowTab.h"
#include "MainUI/FindReplacePlus.h"
#include "Misc/SettingsStoreExtend.h"
#include "Misc/Utility.h"
#include "Misc/FindReplaceQLineEdit.h"
#include "PCRE2/PCREErrors.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/TextResource.h"

static const QString SETTINGS_GROUP = "find_replace_plus";
static const QString REGEX_OPTION_IGNORE_CASE = "(?i)";
static const QString REGEX_OPTION_DOT_ALL = "(?s)";
static const QString REGEX_OPTION_MINIMAL_MATCH = "(?U)";

static const int SHOW_FIND_RESULTS_MESSAGE_DELAY_MS = 20000;

// mappings from LookWhere, Search, and Direction enums to Controls code
// Must be kept in sync with those enums
static const QStringList TGTS = QStringList() << "CF" << "AH"  << "AC" << "SF" << "OP" << "NX";
static const QStringList MDS = QStringList() << "NL" << "RX" << "PS";
static const QStringList DRS = QStringList() << "UP" << "DN";

FindReplacePlus::FindReplacePlus(MainWindow *main_window)
    : QWidget(main_window),
      m_MainWindow(main_window),
      m_OptionWrap(true),
      m_OptionPreview(false),
      m_LookWhereCurrentFile(false),
      m_IsSearchGroupRunning(false),
      m_StartingResource(nullptr),
      m_StartingPos(-1),
      m_InRemainder(false),
      m_SearchRunning(false),
      m_DryRunRunning(false),
      m_ShiftUsed(false),
      m_StartingTextLen(-1)
{
    ui.setupUi(this);
    ExtendUI();
    FindReplaceQLineEdit* prefind_ledit = new FindReplaceQLineEdit(this, true);
    ui.cbPreFind->setLineEdit(prefind_ledit);
    FindReplaceQLineEdit *find_ledit = new FindReplaceQLineEdit(this,true);
    ui.cbFind->setLineEdit(find_ledit);
    FindReplaceQLineEdit *replace_ledit = new FindReplaceQLineEdit(this,true);
    replace_ledit->setTokeniseEnabled(false);
    ui.cbReplace->setLineEdit(replace_ledit);
    QCompleter* pfqc = ui.cbPreFind->completer();
    pfqc->setCaseSensitivity(Qt::CaseSensitive);
    pfqc->setCompletionMode(QCompleter::PopupCompletion);
    ui.cbPreFind->setCompleter(pfqc);
    QCompleter *fqc = ui.cbFind->completer();
    fqc->setCaseSensitivity(Qt::CaseSensitive);
    fqc->setCompletionMode(QCompleter::PopupCompletion);
    ui.cbFind->setCompleter(fqc);
    QCompleter *rqc = ui.cbReplace->completer();
    rqc->setCaseSensitivity(Qt::CaseSensitive);
    rqc->setCompletionMode(QCompleter::PopupCompletion);
    ui.cbReplace->setCompleter(rqc);
    ExtendUI();
    ConnectSignalsToSlots();
    // ShowHideAdvancedOptions();
    ShowHideMarkedText(false);
    ReadSettings();
    UpdatePreSearchUI();
    m_PreviousSearch.clear();


    //Preview
    ui.chkOptionPreview->setDisabled(true);
    ui.chkOptionPreview->setChecked(false);
    ui.chkOptionPreview->setToolTip(tr("Function not implemented!"));
}

// Destructor
FindReplacePlus::~FindReplacePlus()
{
    WriteSettings();
}

bool FindReplacePlus::ResourceContainsCurrentRegex(Resource* resource)
{
    return SearchOperations::TestInFilePlus(GetPreSearchRegex(), GetSearchRegex(), resource);
}


void FindReplacePlus::SetPreviousSearch()
{
    m_PreviousSearch.clear();
    m_PreviousSearch << ui.cbFind->lineEdit()->text();
    m_PreviousSearch << TGTS.at(GetLookWhere());
    m_PreviousSearch << DRS.at(GetSearchDirection());
    if (GetSearchMode() == SearchMode::SearchMode_PreSearch) {
        m_PreviousSearch << ui.cbPreFind->lineEdit()->text();
    }
    else {
        m_PreviousSearch << QString();
    }
}

bool FindReplacePlus::IsNewSearch()
{
    if (m_PreviousSearch.isEmpty()) return true;
    if (m_PreviousSearch.at(0) != ui.cbFind->lineEdit()->text()) return true;
    if (m_PreviousSearch.at(1) != TGTS.at(GetLookWhere())) return true;
    if (m_PreviousSearch.at(2) != DRS.at(GetSearchDirection())) return true;
    if (GetSearchMode() == SearchMode::SearchMode_PreSearch)
        if (m_PreviousSearch.at(3) != ui.cbPreFind->lineEdit()->text()) return true;
    return false;
}

void FindReplacePlus::SetUpFindText()
{
    Searchable *searchable = GetAvailableSearchable();

    if (searchable) {
        QString selected_text = searchable->GetSelectedText();

        if (!selected_text.isEmpty()) {
            ui.cbFind->setEditText(selected_text);
            // To allow the user to immediately click on Replace, we need to setup the
            // regex match as though the user had clicked on Find.
            searchable->SetUpFindForSelectedText(GetSearchRegex());
        }
    }

    // Find text should be selected by default
    ui.cbFind->lineEdit()->selectAll();
    SetFocusFind();
}

void FindReplacePlus::SetFocusPreFind()
{
    ui.cbPreFind->lineEdit()->setFocus(Qt::ShortcutFocusReason);
}

void FindReplacePlus::SetFocusFind()
{
    ui.cbFind->lineEdit()->setFocus(Qt::ShortcutFocusReason);
}


void FindReplacePlus::SetFocusReplace()
{
    ui.cbReplace->lineEdit()->setFocus(Qt::ShortcutFocusReason);
}

bool FindReplacePlus::HasFocusPreFind()
{
    return ui.cbPreFind->lineEdit()->hasFocus();
}

bool FindReplacePlus::HasFocusFind()
{
    return ui.cbFind->lineEdit()->hasFocus();
}


bool FindReplacePlus::HasFocusReplace()
{
    return ui.cbReplace->lineEdit()->hasFocus();
}


QString FindReplacePlus::GetControls()
{
    QStringList  controls;
    controls << MDS.at(GetSearchMode());
    controls << DRS.at(GetSearchDirection());
    controls << TGTS.at(GetLookWhere());
    return controls.join(' ');
}


inline bool FindReplacePlus::isWhereHTML()
{
    return GetLookWhere() == FindReplacePlus::LookWhere_AllHTMLFiles;
}


inline bool FindReplacePlus::isWhereCSS()
{
    return GetLookWhere() == FindReplacePlus::LookWhere_AllCSSFiles;
}

inline bool FindReplacePlus::isWhereOPF()
{
    return GetLookWhere() == FindReplacePlus::LookWhere_OPFFile;
}

inline bool FindReplacePlus::isWhereNCX()
{
    return GetLookWhere() == FindReplacePlus::LookWhere_NCXFile;
}

inline bool FindReplacePlus::isWhereSelected()
{
    return GetLookWhere() == FindReplacePlus::LookWhere_SelectedFiles;
}

inline bool FindReplacePlus::isWhereCF()
{
    return GetLookWhere() == FindReplacePlus::LookWhere_CurrentFile;
}

void FindReplacePlus::close()
{
    WriteSettingsVisible(false);
    QWidget::close();
}


void FindReplacePlus::show()
{
    WriteSettingsVisible(true);
    clearMessage();
    QWidget::show();
}


void FindReplacePlus::ShowHideMarkedText(bool marked)
{
    if (marked) {
        ui.cbLookWhere->hide();
        ui.MarkedTextIndicator->show();
    } else {
        ui.cbLookWhere->show();
        ui.MarkedTextIndicator->hide();
    }
}

bool FindReplacePlus::IsMarkedText()
{
    return !ui.MarkedTextIndicator->isHidden();
}


void FindReplacePlus::HideFindReplace()
{
    WriteSettingsVisible(false);
    hide();
}

void FindReplacePlus::DoRestart()
{
    m_PreviousSearch.clear();
}

void FindReplacePlus::RestartClicked()
{
    m_PreviousSearch.clear();
    ShowMessage(tr("Search will restart"));
}

void FindReplacePlus::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        HideFindReplace();
    }
}


void FindReplacePlus::ShowMessage(const QString &message)
{
    QString new_message = message;

    if (m_LookWhereCurrentFile && !isWhereCF()) {
        new_message.append(" (" % tr("Current File") % ")");
    }

    ui.message->setText(new_message);
    m_timer.start(SHOW_FIND_RESULTS_MESSAGE_DELAY_MS);
    emit ShowMessageRequest(new_message);
}

void FindReplacePlus::SetKeyModifiers()
{
    // Only use with mouse click not menu/shortcuts to avoid modifying actions
    m_LookWhereCurrentFile = QApplication::keyboardModifiers() & Qt::ControlModifier;
    m_ShiftUsed = QApplication::keyboardModifiers() & Qt::ShiftModifier;
}

void FindReplacePlus::ResetKeyModifiers()
{
    m_LookWhereCurrentFile = false;
    m_ShiftUsed = false;
}

void FindReplacePlus::FindClicked()
{
    SetKeyModifiers();
    Find();
    ResetKeyModifiers();
}

void FindReplacePlus::ReplaceClicked()
{
    if (!IsValidFindText())
        return warningEmptyExpression();

    // This is really ReplaceFind";
    SetKeyModifiers();
    Replace();
    ResetKeyModifiers();
}

void FindReplacePlus::ReplaceAllClicked()
{
    if (!IsValidFindText())
        return warningEmptyExpression();

    //SetKeyModifiers();
    if (m_ShiftUsed || m_OptionPreview) {
        ChooseReplacements();
    } else {
        ReplaceAll();
    }
    ResetKeyModifiers();
}

void FindReplacePlus::CountClicked()
{
    if (!IsValidFindText())
        return warningEmptyExpression();

    //SetKeyModifiers();
    if (m_ShiftUsed || m_OptionPreview) {
        PerformDryRunReplace();
    } else {
        Count();
    }
    ResetKeyModifiers();
}


bool FindReplacePlus::FindAnyText(QString text, bool escape)
{
    // bool had_focus = HasFocus();
    SetCodeViewIfNeeded();
    WriteSettings();

    SetSearchMode(FindReplacePlus::SearchMode_Regex);
    SetLookWhere(FindReplacePlus::LookWhere_AllHTMLFiles);
    SetSearchDirection(FindReplacePlus::Direction_Down);
    // SetOptionWrap(true);

    QString search_text;
    if (escape) {
        search_text = QRegularExpression::escape(text);
    } else {
        search_text = text + "(?![^<>]*>)(?!.*<body[^>]*>)";
    }
    ui.cbFind->setEditText(search_text);
    bool found = Find();
    ReadSettings();
    // Show the search term in case it's needed
    ui.cbFind->setEditText(search_text);
    // RestoreFRFocusIfNeeded(had_focus, true);
    return found;
}

void FindReplacePlus::FindAnyTextInTags(QString text)
{
    // bool had_focus = HasFocus();
    SetCodeViewIfNeeded();
    WriteSettings();

    SetSearchMode(FindReplacePlus::SearchMode_Regex);
    SetLookWhere(FindReplacePlus::LookWhere_AllHTMLFiles);
    SetSearchDirection(FindReplacePlus::Direction_Down);
    // SetOptionWrap(true);
    text = text + "(?=[^<]*>)(?!(?:[^<\"]*\"[^<\"]*\")+\\s*/?>)";
    ui.cbFind->setEditText(text);
    Find();

    ReadSettings();
    // RestoreFRFocusIfNeeded(had_focus, true);
}


bool FindReplacePlus::DoFindNext()
{
    if (m_SearchRunning) return false;
    m_SearchRunning = true;
    SetSearchDirection(FindReplacePlus::Direction_Down);
    bool found = Find();
    m_SearchRunning = false;
    return found;
}

bool FindReplacePlus::DoFindPrevious()
{
    if (m_SearchRunning) return	false;
    m_SearchRunning = true;
    SetSearchDirection(FindReplacePlus::Direction_Up);
    bool found = Find();
    m_SearchRunning = false;
    return found;
}

bool FindReplacePlus::DoReplaceNext()
{
    if (m_SearchRunning) return	false;
    m_SearchRunning = true;
    SetSearchDirection(FindReplacePlus::Direction_Down);
    bool found = Replace();
    m_SearchRunning = false;
    return found;

}

bool FindReplacePlus::DoReplacePrevious()
{
    if (m_SearchRunning) return	false;
    m_SearchRunning = true;
    SetSearchDirection(FindReplacePlus::Direction_Up);
    bool found = Replace();
    m_SearchRunning = false;
    return found;
}

bool FindReplacePlus::Find()
{
    if (IsNewSearch()) {
        SetPreviousSearch();
        if (!SetStartingResource(true)) {
            SearchEndingProcess();
            return false;
        }
    }
    bool found = FindText(GetSearchDirection());
    return found;
}

// Counts the number of occurrences of the user's
// term in the document.
int FindReplacePlus::Count()
{
    clearMessage();

    // count changes nothing and should not change
    // current file and position, nor update previous search

    if (!IsValidFindText()) {
        return 0;
    }

    if (IsNewSearch()) {
        SetPreviousSearch();
        if (!SetStartingResource())
            return 0;
    }

    // bool had_focus = HasFocus();
    SetCodeViewIfNeeded();
    int count = 0;

    if (isWhereCF() || m_LookWhereCurrentFile || IsMarkedText()) {
        Searchable *searchable = GetAvailableSearchable();

        if (!searchable) {
            return 0;
        }

        count = searchable->CountPlus(GetPreSearchRegex(), GetSearchRegex(), IsMarkedText());
    } else {
        count = CountInFiles();
    }

    if (count == 0) {
        CannotFindSearchTerm();
    } else if (count > 0) {
        QString message = tr("Matches found: %n", "", count);
        ShowMessage(message);
    }

    UpdatePreviousPreFindStrings();
    UpdatePreviousFindStrings();
    // RestoreFRFocusIfNeeded(had_focus, true);
    return count;
}


bool FindReplacePlus::Replace()
{
    if (IsNewSearch()) {
        SetPreviousSearch();
        if (!SetStartingResource(true)) return false;
    }

    bool found = false;
    found = ReplaceText(GetSearchDirection());
    return found;
}

bool FindReplacePlus::ReplaceCurrent()
{
    // isNewSearch should always return false here
    // as search must have already found something to replace

    bool found = false;

    if (GetSearchDirection() == FindReplacePlus::Direction_Up) {
        found = ReplaceText(Direction::Direction_Up, true);
    } else {
        found = ReplaceText(Direction::Direction_Down, true);
    }

    return found;
}


// Builds a Find All table
void FindReplacePlus::PerformDryRunReplace()
{
    return; // Function not implemented
    if (m_DryRunRunning) return;
    m_DryRunRunning = true;

    m_MainWindow->GetCurrentContentTab()->SaveTabContent();

    if (IsNewSearch()) {
        SetStartingResource();
        SetPreviousSearch();
    }

    if (!IsValidFindText()) return;

    bool plus_mode = true;
    DryRunReplace*  dr = new DryRunReplace(plus_mode,this);
    connect(dr, &QWidget::destroyed, this, &FindReplacePlus::DryRunComplete);
    dr->CreateTable();
    // do this non-modally
    dr->show();
    dr->raise();
    dr->activateWindow();
}

// Allows you to delete unwanted replacements and apply those remaining
void FindReplacePlus::ChooseReplacements()
{
    return; // Function not implemented
    /*
    m_MainWindow->GetCurrentContentTab()->SaveTabContent();
    ShowMessage(tr("Choose Replacements"));

    if (IsNewSearch()) {
        SetStartingResource();
        SetPreviousSearch();
    }

    if (!IsValidFindText()) return;

    // must be modal to prevent crashes and nonsense
    ReplacementPreviewPlus rc(this);
    rc.CreateTable();
    rc.exec();

    clearMessage();

    int count = rc.GetReplacementCount();
    if (count == 0) {
        ShowMessage(tr("No replacements made"));
    } else if (count > 0) {
        QString message = tr("Replacements made: %n", "", count);
        ShowMessage(message);
    }

    if (count > 0) {
        // Signal that the contents have changed and update the view
        m_MainWindow->GetCurrentBook()->SetModified(true);
        m_MainWindow->GetCurrentContentTab()->ContentChangedExternally();
    }

    UpdatePreviousPreFindStrings();
    UpdatePreviousFindStrings();
    UpdatePreviousReplaceStrings();
    */
}


// Replaces the user's search term with the user's
// replacement text in the entire document.
int FindReplacePlus::ReplaceAll()
{
    m_MainWindow->GetCurrentContentTab()->SaveTabContent();
    clearMessage();

    if (IsNewSearch()) {
        SetPreviousSearch();
        if (!SetStartingResource()) return 0;
    }

    if (!IsValidFindText()) {
        return 0;
    }

    // bool had_focus = HasFocus();
    SetCodeViewIfNeeded();
    int count = 0;

    if (isWhereCF() || m_LookWhereCurrentFile || IsMarkedText()) {
        Searchable *searchable = GetAvailableSearchable();

        if (!searchable) {
            return 0;
        }

        count = searchable->ReplaceAllPlus(GetPreSearchRegex(), GetSearchRegex(), ui.cbReplace->lineEdit()->text(), IsMarkedText());
    } else {
        count = ReplaceInAllFiles();

    }

    if (count == 0) {
        ShowMessage(tr("No replacements made"));
    } else if (count > 0) {
        QString message = tr("Replacements made: %n", "", count);
        ShowMessage(message);
    }

    if (count > 0) {
        // Signal that the contents have changed and update the view
        m_MainWindow->GetCurrentBook()->SetModified(true);
        m_MainWindow->GetCurrentContentTab()->ContentChangedExternally();
    }

    UpdatePreviousPreFindStrings();
    UpdatePreviousFindStrings();
    UpdatePreviousReplaceStrings();
    // RestoreFRFocusIfNeeded(had_focus, true);
    return count;
}

void FindReplacePlus::FindNextInFile()
{
    m_LookWhereCurrentFile = true;
    FindText(Direction::Direction_Down);
    m_LookWhereCurrentFile = false;
}

void FindReplacePlus::ReplaceNextInFile()
{
    m_LookWhereCurrentFile = true;
    ReplaceText(Direction::Direction_Down);
    m_LookWhereCurrentFile = false;
}

void FindReplacePlus::ReplaceAllInFile()
{
    m_LookWhereCurrentFile = true;
    ReplaceAll();
    m_LookWhereCurrentFile = false;
}

void FindReplacePlus::CountInFile()
{
    m_LookWhereCurrentFile = true;
    Count();
    m_LookWhereCurrentFile = false;
}

void FindReplacePlus::clearMessage()
{
    if (!m_IsSearchGroupRunning) {
        ui.message->clear();
        emit ShowMessageRequest("");
    }
}

void FindReplacePlus::expireMessage()
{
    m_timer.stop();
    ui.message->clear();
    emit ShowMessageRequest("");
}

bool FindReplacePlus::GetSearchableAndFindNext(bool marked_text, int split_at, bool inRemainder)
{
    bool found = false;
    Searchable* searchable = GetAvailableSearchable();
    if (!searchable) {
        return found;
    }
    if (GetSearchDirection() == FindReplacePlus::Direction_Up) {
        found = searchable->FindPrevPlus(GetPreSearchRegex(), GetSearchRegex(), marked_text, split_at, inRemainder);
    }
    else {
        found = searchable->FindNextPlus(GetPreSearchRegex(), GetSearchRegex(), marked_text, split_at, inRemainder);
    }
    return found;
}

// Starts the search for the user's term.
bool FindReplacePlus::FindText(Direction direction)
{
    bool found = false;
    clearMessage();

    if (!IsValidFindText()) {
        warningEmptyExpression();
        return found;
    }

    // bool had_focus = HasFocus();
    SetCodeViewIfNeeded();

    if (isWhereCF() || m_LookWhereCurrentFile || IsMarkedText()) {
        found = GetSearchableAndFindNext(IsMarkedText(),m_StartingPos,m_InRemainder);
        if (!found) {
            // inRemainder would be true when the search run to the end of
            // starting resource but leave a section of text has not searched yet.
            if (!m_InRemainder) {
                m_InRemainder = true;
                found = GetSearchableAndFindNext(IsMarkedText(), m_StartingPos, m_InRemainder);
            }
            if (!found && m_OptionWrap) {
                SetStartingResource();
                found = GetSearchableAndFindNext(IsMarkedText(), m_StartingPos, m_InRemainder);
            }
        }

    } else {
        found = FindInAllFiles(direction);
    }

    if (found) {
        clearMessage();
    } else {
        ShowMessage(tr("End of search"));
        if (!m_IsSearchGroupRunning)
            warningEndedSearch();
        RestartClicked();
    }

    UpdatePreviousPreFindStrings();
    UpdatePreviousFindStrings();

    return found;
}


// Replaces the user's search term with the user's
// replacement text if a match is selected. If it's not,
// calls Find in the direction specified so it becomes selected.
bool FindReplacePlus::ReplaceText(Direction direction, bool replace_current)
{
    bool found = false;
    clearMessage();

    if (!IsValidFindText()) {
        return found;
    }

    // bool had_focus = HasFocus();
    SetCodeViewIfNeeded();
    Searchable *searchable = GetAvailableSearchable();

    if (!searchable) {
        return found;
    }

    // If we have the matching text selected, replace it
    // This will not do anything if matching text is not selected.
    Searchable::Direction direction_ = direction == Direction_Down ? Searchable::Direction_Down : Searchable::Direction_Up;
    found = searchable->ReplaceSelectedPlus(GetSearchRegex(), ui.cbReplace->lineEdit()->text(), direction_, replace_current);

    // If we are not going to stay put after a simple Replace, then find next match.
    if (!replace_current) {
        // If doing a Replace/Find set the value of found to the result of the Find.
        found = FindText(GetSearchDirection());
    }

    UpdatePreviousPreFindStrings();
    UpdatePreviousFindStrings();
    UpdatePreviousReplaceStrings();
    // RestoreFRFocusIfNeeded(had_focus, true);
    // Do not use the return value to tell if a replace was done - only if a complete
    // Find/Replace or ReplaceCurrent was ok.  This allows multiple selections to work as expected.
    return found;
}

void FindReplacePlus::SetCodeViewIfNeeded()
{
    bool setCodeViewNeeded = false;
    if (HasFocusFind()) {
        ui.cbFind->lineEdit()->clearFocus();
        setCodeViewNeeded = true;
    }
    else if (HasFocusReplace()) {
        // give the current tab CodeView Tab the focus
        ui.cbReplace->lineEdit()->clearFocus();
        setCodeViewNeeded = true;
    }
    else if (HasFocusPreFind()) {
        ui.cbPreFind->lineEdit()->clearFocus();
        setCodeViewNeeded = true;
    }
    if (setCodeViewNeeded) {
        // give the current tab CodeView Tab the focus
        ContentTab* current_tab = m_MainWindow->GetCurrentContentTab();
        if (current_tab) current_tab->setFocus();
    }
}

// Displays a message to the user informing him
// that his last search term could not be found.
void FindReplacePlus::CannotFindSearchTerm()
{
    ShowMessage(tr("No matches found"));
}

QString FindReplacePlus::GetPreSearchRegex()
{
    if (GetSearchMode() != SearchMode::SearchMode_PreSearch)
        return QString();
    QString text = ui.cbPreFind->lineEdit()->text();
    text.replace(QRegularExpression("\\R"), "\n");
    return text;
}

// Constructs a searching regex from the selected
// options and fields and then returns it.
QString FindReplacePlus::GetSearchRegex()
{
    QString text = ui.cbFind->lineEdit()->text();
    // Convert &#x2029; to match line separator used by plainText.
    text.replace(QRegularExpression("\\R"), "\n");

    QString search(text);

    // Search type
    if (GetSearchMode() == FindReplacePlus::SearchMode_Normal) {
        search = QRegularExpression::escape(search);
    }
    // qDebug() << "GetSearchRegex returns: " << search;
    return search;
}


QString FindReplacePlus::GetReplace()
{
    return ui.cbReplace->lineEdit()->text();
}

QList<Resource*> FindReplacePlus::GetAllResourcesToSearch()
{
    QList<Resource *> resources;

    if (isWhereCF() || m_LookWhereCurrentFile) {
        resources << GetCurrentResource();
    } else {
        resources = GetFilesToSearch(true);
    }
    return resources;
}

void FindReplacePlus::EmitOpenFileRequest(const QString& bookpath, int line, int pos)
{
    emit FROpenFileRequest(bookpath, line, pos);
}

bool FindReplacePlus::IsCurrentFileInSelection()
{
    bool found = false;
    QList <Resource *> resources = GetFilesToSearch();
    if (resources.isEmpty()) return false;
    Resource *current_resource = GetCurrentResource();
    if (!current_resource) return false;
    return resources.contains(current_resource);
}


// Returns all resources according to LookWhere setting
QList <Resource *> FindReplacePlus::GetFilesToSearch(bool force_all)
{
    QList <Resource *> all_resources;
    QList <Resource *> resources;
    LookWhere lookwhere = GetLookWhere();

    switch (lookwhere) {
    case FindReplacePlus::LookWhere_CurrentFile:
        all_resources << GetCurrentResource();
        break;
    case FindReplacePlus::LookWhere_AllHTMLFiles:
        all_resources = m_MainWindow->GetAllHTMLResources();
        break;
    case FindReplacePlus::LookWhere_AllCSSFiles:
        all_resources = m_MainWindow->GetAllCSSResources();
        break;
    case FindReplacePlus::LookWhere_SelectedFiles:
        all_resources = m_MainWindow->GetBookBrowserSelectedResources();
        break;
    case FindReplacePlus::LookWhere_OPFFile:
        all_resources = m_MainWindow->GetOPFResource();
        break;
    case FindReplacePlus::LookWhere_NCXFile:
        all_resources = m_MainWindow->GetNCXResource();
        break;
    }
    return all_resources;
}


int FindReplacePlus::CountInFiles()
{
    m_MainWindow->GetCurrentContentTab()->SaveTabContent();

    QList<Resource *>search_files = GetFilesToSearch(true);
    if (search_files.isEmpty()) return 0;

    return SearchOperations::CountInFilesPlus(GetPreSearchRegex(), GetSearchRegex(), search_files);
}

int FindReplacePlus::ReplaceInAllFiles()
{
    m_MainWindow->GetCurrentContentTab()->SaveTabContent();
    QList<Resource *>search_files = GetFilesToSearch(true);
    if (search_files.isEmpty()) return 0;

    int count = SearchOperations::ReplaceInAllFIlesPlus(
                    GetPreSearchRegex(),
                    GetSearchRegex(),
                    ui.cbReplace->lineEdit()->text(),
                    search_files);
    return count;
}


bool FindReplacePlus::FindInAllFiles(Direction direction)
{
    Searchable *searchable = 0;
    bool found = false;
    Resource * current_resource = GetCurrentResource();
    bool isFinish = false;

    // The first current resource must be starting resource and it must contains at least one match,
    // because we have set it in SetStartingResource() by force.
    if (current_resource == m_StartingResource) {
        found = GetSearchableAndFindNext(false, m_StartingPos, m_InRemainder);
        if (!found) {
            if (!m_InRemainder) {
                m_InRemainder = true;
            } else {
                isFinish = true;
                m_InRemainder = false;
            }
        }
    }
    else if (IsCurrentFileInSelection()){
        found = GetSearchableAndFindNext(false, -1, m_InRemainder);
    }

    if (!isFinish && !found) {
        Resource* next_resource = GetNextContainingResource(direction);
        if (!next_resource) return found;

        if (next_resource != m_StartingResource) {
            int split_at = -1;
            OpenResourceTabForSearch(next_resource);
            found = GetSearchableAndFindNext(false, split_at, m_InRemainder);
        }
        else {
            if (next_resource != current_resource)
                OpenResourceTabForSearch(next_resource);

            if (m_InRemainder && m_StartingPos != -1) {
                found = GetSearchableAndFindNext(false, m_StartingPos, m_InRemainder);
            }

            if (!found) isFinish = true;
        }
    }
    if (isFinish && m_OptionWrap) {
        SetStartingResource();
        found = GetSearchableAndFindNext(false, m_StartingPos, m_InRemainder);
    }
    return found;
}

Resource* FindReplacePlus::GetNextContainingResource(Direction direction)
{
    Resource* current_resource = GetCurrentResource();
    Resource* starting_resource = NULL;
    bool need_to_check_assigned_starting_resource = false;

    // if CurrentFile is the same type as LookWhere, set it as the starting resource
    if (isWhereHTML() && (current_resource->Type() == Resource::HTMLResourceType)) {
        starting_resource = current_resource;
    }
    else if (isWhereCSS() && (current_resource->Type() == Resource::CSSResourceType)) {
        starting_resource = current_resource;
    }
    else if (isWhereOPF() && (current_resource->Type() == Resource::OPFResourceType)) {
        starting_resource = current_resource;
    }
    else if (isWhereNCX() && (current_resource->Type() == Resource::NCXResourceType)) {
        starting_resource = current_resource;
    }
    else if (isWhereSelected()) {
        starting_resource = current_resource;
    }

    QList<Resource*> resources = GetFilesToSearch();
    if (resources.isEmpty()) return NULL;

    // If no starting resource we will need to set one first and then search it.
    // Otherwise,  this resource was part of the current selection (or earlier) and has already been
    // searched in FindInAllFiles
    bool do_not_need_get_next = false;
    bool test_starting_resource = false;
    if (!starting_resource || (isWhereSelected() && !IsCurrentFileInSelection())) {
        m_StartingPos = -1;
        do_not_need_get_next = true;
        test_starting_resource = true;
        if (direction == Direction::Direction_Down) {
            starting_resource = resources.first();
        }
        else {
            starting_resource = resources.last();
        }
    }
    Resource* next_resource = starting_resource;

    int i = 0;
    while (i < resources.count()) {
        if (!do_not_need_get_next) {
            next_resource = GetNextResource(next_resource, direction);
        }
        if (next_resource != NULL) {
            if (ResourceContainsCurrentRegex(next_resource)) {
                if (test_starting_resource) {
                    m_StartingResource = next_resource;
                    test_starting_resource = false;
                }
                return next_resource;
            }
            // Reach starting resource again but could not find anything.
            else if (!test_starting_resource && next_resource == m_StartingResource) {
                if (!m_OptionWrap) return NULL;
            }
        }
        else {
            return NULL;
        }
        do_not_need_get_next = false;
        i++;
    }

    return NULL;
}


Resource *FindReplacePlus::GetNextResource(Resource *current_resource, Direction direction)
{
    QList <Resource*> resources = GetFilesToSearch(true);
    int max_reading_order       = resources.count() - 1;
    int current_reading_order   = 0;
    int next_reading_order      = 0;

    if (resources.isEmpty()) return NULL;

    int _index = resources.indexOf(current_resource);
    if (_index >= 0) current_reading_order = _index;

    // We wrap back (if needed)
    if (direction == Direction::Direction_Up) {
        next_reading_order = current_reading_order - 1 >= 0 ? current_reading_order - 1 : max_reading_order ;
    } else {
        next_reading_order = current_reading_order + 1 <= max_reading_order ? current_reading_order + 1 : 0;
    }
    if (next_reading_order > max_reading_order || next_reading_order < 0) {
        return NULL;
    } else {
        Resource* nextres = resources[ next_reading_order ];
        return nextres;
    }
}

void FindReplacePlus::OpenResourceTabForSearch(Resource* resource)
{
    // Save if editor or F&R has focus
    bool has_focus_prefind = HasFocusPreFind();
    bool has_focus_find = HasFocusFind();
    bool has_focus_replace = HasFocusReplace();

    // Save selected resources since opening tabs changes selection
    QList<Resource*>selected_resources = m_MainWindow->GetBookBrowserSelectedResources();

    m_MainWindow->OpenResourceAndWaitUntilLoaded(resource);
    // give the new tab initial focus
    ContentTab* current_tab = m_MainWindow->GetCurrentContentTab();
    if (current_tab) current_tab->setFocus();
    // Restore selection since opening tabs changes selection
    if (isWhereSelected()) {
        m_MainWindow->SelectResources(selected_resources);
    }
    // Reset focus to F&R if it had it
    if (has_focus_prefind) SetFocusPreFind();
    if (has_focus_find) SetFocusFind();
    if (has_focus_replace) SetFocusReplace();
}

Resource *FindReplacePlus::GetCurrentResource()
{
    return m_MainWindow->GetCurrentContentTab()->GetLoadedResource();
}


QStringList FindReplacePlus::GetPreviousPreFindStrings()
{
    QStringList find_strings;
    for (int i = 0; i < qMin(ui.cbPreFind->count(), ui.cbPreFind->maxCount()); i++) {
        if (!find_strings.contains(ui.cbPreFind->itemText(i))) {
            find_strings.append(ui.cbPreFind->itemText(i));
        }
    }
    return find_strings;
}

QStringList FindReplacePlus::GetPreviousFindStrings()
{
    QStringList find_strings;
    for (int i = 0; i < qMin(ui.cbFind->count(), ui.cbFind->maxCount()); i++) {
        if (!find_strings.contains(ui.cbFind->itemText(i))) {
            find_strings.append(ui.cbFind->itemText(i));
        }
    }
    return find_strings;
}

QStringList FindReplacePlus::GetPreviousReplaceStrings()
{
    QStringList replace_strings;

    for (int i = 0; i < qMin(ui.cbReplace->count(), ui.cbReplace->maxCount()); i++) {
        if (!replace_strings.contains(ui.cbReplace->itemText(i))) {
            replace_strings.append(ui.cbReplace->itemText(i));
        }
    }

    return replace_strings;
}

void FindReplacePlus::UpdatePreviousPreFindStrings(const QString& text)
{
    if (GetSearchMode() != SearchMode::SearchMode_PreSearch)
        return;

    QString new_find_string;

    if (!text.isNull()) {
        new_find_string = text;
    }
    else {
        new_find_string = ui.cbPreFind->lineEdit()->text();
    }

    int used_at_index = ui.cbPreFind->findText(new_find_string);

    if (used_at_index != -1) {
        ui.cbPreFind->removeItem(used_at_index);
    }

    ui.cbPreFind->insertItem(0, new_find_string);
    // Must not change the current string!
    ui.cbPreFind->setCurrentIndex(0);
}

void FindReplacePlus::UpdatePreviousFindStrings(const QString &text)
{
    QString new_find_string;

    if (!text.isNull()) {
        new_find_string = text;
    } else {
        new_find_string = ui.cbFind->lineEdit()->text();
    }

    int used_at_index = ui.cbFind->findText(new_find_string);

    if (used_at_index != -1) {
        ui.cbFind->removeItem(used_at_index);
    }

    ui.cbFind->insertItem(0, new_find_string);
    // Must not change the current string!
    ui.cbFind->setCurrentIndex(0);
}


void FindReplacePlus::UpdatePreviousReplaceStrings(const QString &text)
{
    QString new_replace_string;

    if (!text.isNull()) {
        new_replace_string = text;
    } else {
        new_replace_string = ui.cbReplace->lineEdit()->text();
    }

    int used_at_index = ui.cbReplace->findText(new_replace_string);

    if (used_at_index != -1) {
        ui.cbReplace->removeItem(used_at_index);
    }

    ui.cbReplace->insertItem(0, new_replace_string);
    // Must not change the current string!
    ui.cbReplace->setCurrentIndex(0);
}


void FindReplacePlus::UpdateSearchControls(const QString &text)
{
    if (text.isEmpty()) return;

    // Search Mode
    if (text.contains("NL")) {
        SetSearchMode(FindReplacePlus::SearchMode_Normal);
    } else if (text.contains("RX")) {
        SetSearchMode(FindReplacePlus::SearchMode_Regex);
    } else if (text.contains("PS")) {
        SetSearchMode(FindReplacePlus::SearchMode_PreSearch);
    }

    // Search LookWhere
    if (text.contains("CF")) {
        SetLookWhere(FindReplacePlus::LookWhere_CurrentFile);
    }
    else if (text.contains("AH")) {
        SetLookWhere(FindReplacePlus::LookWhere_AllHTMLFiles);
    }
    else if (text.contains("AC")) {
        SetLookWhere(FindReplacePlus::LookWhere_AllCSSFiles);
    }
    else if (text.contains("OP")) {
        SetLookWhere(FindReplacePlus::LookWhere_OPFFile);
    }
    else if (text.contains("NX")) {
        SetLookWhere(FindReplacePlus::LookWhere_NCXFile);
    }

    // Search Direction
    if (text.contains("UP")) {
        SetSearchDirection(FindReplacePlus::Direction_Up);
    } else if (text.contains("DN")) {
        SetSearchDirection(FindReplacePlus::Direction_Down);
    }

    // Search Flags
    //SetOptionWrap(text.contains("WR"));
}

void FindReplacePlus::SearchEndingProcess()
{
    UpdatePreviousPreFindStrings();
    UpdatePreviousFindStrings();
    ShowMessage(tr("End of search"));
    if (!m_IsSearchGroupRunning)
        warningEndedSearch();
    RestartClicked();
}

FindReplacePlus::SearchMode FindReplacePlus::GetSearchMode()
{
    int mode = ui.cbSearchMode->itemData(ui.cbSearchMode->currentIndex()).toInt();

    switch (mode) {
        case FindReplacePlus::SearchMode_Regex:
        case FindReplacePlus::SearchMode_PreSearch:
            return static_cast<FindReplacePlus::SearchMode>(mode);
            break;
        default:
            return FindReplacePlus::SearchMode_Normal;
    }
}

FindReplacePlus::LookWhere FindReplacePlus::GetLookWhere()
{
    int look = ui.cbLookWhere->itemData(ui.cbLookWhere->currentIndex()).toInt();

    switch (look) {
        case FindReplacePlus::LookWhere_AllHTMLFiles:
        case FindReplacePlus::LookWhere_AllCSSFiles:
        case FindReplacePlus::LookWhere_SelectedFiles:
        case FindReplacePlus::LookWhere_OPFFile:
        case FindReplacePlus::LookWhere_NCXFile:
            return static_cast<FindReplacePlus::LookWhere>(look);
            break;

        default:
            return FindReplacePlus::LookWhere_CurrentFile;
    }
}

FindReplacePlus::Direction FindReplacePlus::GetSearchDirection()
{
    int direction = ui.cbSearchDirection->itemData(ui.cbSearchDirection->currentIndex()).toInt();

    switch (direction) {
        case FindReplacePlus::Direction_Up:
            return static_cast<FindReplacePlus::Direction>(direction);
            break;

        default:
            return FindReplacePlus::Direction_Down;
    }
}


bool FindReplacePlus::IsValidFindText()
{
    return  !ui.cbFind->lineEdit()->text().isEmpty();
}

void FindReplacePlus::ReadSettings()
{
    SettingsStoreExtend settings;
    settings.beginGroup(SETTINGS_GROUP);

    // Set F&R Buttons to text-only if requested
    bool frButtonsTextOnly = settings.value("frbuttonstextonly", false).toBool();
    if (frButtonsTextOnly) {
        SetFRButtonsTextOnly();
    }

    // Find and Replace history
    QStringList prefind_strings = settings.value("prefind_strings").toStringList();
    prefind_strings.removeDuplicates();
    ui.cbPreFind->clear();
    ui.cbPreFind->addItems(prefind_strings);
    QStringList find_strings = settings.value("find_strings").toStringList();
    find_strings.removeDuplicates();
    ui.cbFind->clear();
    ui.cbFind->addItems(find_strings);
    QStringList replace_strings = settings.value("replace_strings").toStringList();
    replace_strings.removeDuplicates();
    ui.cbReplace->clear();
    ui.cbReplace->addItems(replace_strings);
    SetSearchMode(settings.value("search_mode", 0).toInt());
    SetLookWhere(settings.value("look_where", 0).toInt());
    SetSearchDirection(settings.value("search_direction", 0).toInt());
    bool optionWrap = settings.value("optionwrap", true).toBool();
    SetOptionWrap(optionWrap);
    //bool optionPreview = settings.value("optionpreview", false).toBool();
    //SetOptionPreview(optionPreview);
    settings.endGroup();
}

void FindReplacePlus::ShowHide()
{
    if (m_MainWindow->GetFindReplaceMode() != MainWindow::FindReplaceMode::EnhancedMode) {
        hide();
        return;
    }

    SettingsStoreExtend settings;
    settings.beginGroup(SETTINGS_GROUP);
    QVariant show_find_replace = settings.value("visible");
    settings.endGroup();

    // Hide the window by default
    if (show_find_replace.isNull() ? false : show_find_replace.toBool()) {
        show();
    } else {
        hide();
    }
}

void FindReplacePlus::WriteSettingsVisible(bool visible)
{
    SettingsStoreExtend settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue("visible", visible);
    settings.endGroup();
}


void FindReplacePlus::WriteSettings()
{
    SettingsStoreExtend settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue("prefind_strings", GetPreviousPreFindStrings());
    settings.setValue("find_strings", GetPreviousFindStrings());
    settings.setValue("replace_strings", GetPreviousReplaceStrings());
    settings.setValue("search_mode", GetSearchMode());
    settings.setValue("look_where", GetLookWhere());
    settings.setValue("search_direction", GetSearchDirection());
    //settings.setValue("optionwrap", ui.chkOptionWrap->isChecked());
    //settings.setValue("optionpreview", ui.chkOptionPreview->isChecked());
    settings.endGroup();
}


Searchable *FindReplacePlus::GetAvailableSearchable()
{
    Searchable *searchable = m_MainWindow->GetCurrentContentTab()->GetSearchableContent();
    if (!searchable) {
        ShowMessage(tr("This tab cannot be searched"));
    }

    return searchable;
}


void FindReplacePlus::SaveSearchAction()
{
    SearchEditorModelPlus::searchEntry *search_entry = new SearchEditorModelPlus::searchEntry();
    search_entry->name = "Unnamed Search";
    search_entry->is_group = false;
    search_entry->prefind = ui.cbPreFind->lineEdit()->text();;
    search_entry->find = ui.cbFind->lineEdit()->text();
    search_entry->replace = ui.cbReplace->lineEdit()->text();
    search_entry->controls = GetControls();
    if (GetSearchMode() != SearchMode::SearchMode_PreSearch)
        search_entry->prefind = QString();
    emit OpenSearchEditorRequest(search_entry);
}


void FindReplacePlus::LoadSearchByName(const QString &name)
{
    // callers to SearchEditorModel's GetEntryFromName receive a searchEntry pointer
    // created by a call to new and must take ownership and so must clean up after themselves
    SearchEditorModelPlus::searchEntry * search_entry = SearchEditorModelPlus::instance()->GetEntryFromName(name);
    if (search_entry) {
        LoadSearch(search_entry);
        delete search_entry;
    }
}

// LoadSearch is NOT the owner of any passed in search entry pointers
void FindReplacePlus::LoadSearch(SearchEditorModelPlus::searchEntry *search_entry)
{
    if (!search_entry) {
        clearMessage();
        return;
    }

    UpdatePreviousPreFindStrings(search_entry->prefind);
    UpdatePreviousFindStrings(search_entry->find);
    UpdatePreviousReplaceStrings(search_entry->replace);
    UpdateSearchControls(search_entry->controls);

    // Show a message containing the name that was loaded
    QString message(tr("Unnamed search loaded"));

    if (!search_entry->name.isEmpty()) {
        message = QString("%1: %2 ").arg(tr("Loaded")).arg(search_entry->name.replace('<', "&lt;").replace('>', "&gt;").left(50));
    }
    ShowMessage(message);
}

int FindReplacePlus::GetStartingPos()
{
    int origin_cursor_pos = m_MainWindow->GetCurrentContentTab()->GetCursorPosition();
    return SearchOperations::GetSuitableStartPos(GetPreSearchRegex(),
                                                 GetSearchRegex(),
                                                 GetCurrentResource(),
                                                 origin_cursor_pos,
                                                 GetSearchDirection() == Direction_Up);
}

bool FindReplacePlus::SetStartingResource(bool open_starting_tab)
{
    if (isWhereCF() || m_LookWhereCurrentFile || IsMarkedText()) {
        m_InRemainder = false;
        m_StartingPos = GetStartingPos();
        return true;
    }
    Resource * current_resource = GetCurrentResource();
    m_StartingResource = nullptr;

    if (!IsCurrentFileInSelection() || !ResourceContainsCurrentRegex(current_resource)) {
        m_StartingResource = GetNextContainingResource(GetSearchDirection());
        if (!m_StartingResource) return false;
        if (open_starting_tab) OpenResourceTabForSearch(m_StartingResource);
    }
    else {
        m_StartingResource = current_resource;
    }

    m_StartingPos = -1;
    m_InRemainder = false;
    if (m_StartingResource == current_resource) {
        m_StartingPos = GetStartingPos();
    }
    return true;
}

// These are *Search methods are invoked by the SearchEditor
void FindReplacePlus::FindSearch()
{
    // these entries are owned by the Search Editor who will clean up as needed
    QList<SearchEditorModelPlus::searchEntry*> search_entries = m_MainWindow->SearchEditorGetCurrentEntriesPlus();

    if (search_entries.isEmpty()) {
        emit AskWhyGetEmptyEntries();
        return;
    }

    SetKeyModifiers();
    m_IsSearchGroupRunning = true;
    foreach(SearchEditorModelPlus::searchEntry * search_entry, search_entries) {
        LoadSearch(search_entry);
        if (Find()) {
            break;
        } else {
            m_MainWindow->SearchEditorRecordEntryAsCompletedPlus(search_entry);
        }
    }
    m_IsSearchGroupRunning = false;
    ResetKeyModifiers();
}

void FindReplacePlus::ReplaceCurrentSearch()
{
    // these entries are owned by the Search Editor who will clean up as needed
    QList<SearchEditorModelPlus::searchEntry*> search_entries = m_MainWindow->SearchEditorGetCurrentEntriesPlus();

    if (search_entries.isEmpty()) {
        emit AskWhyGetEmptyEntries();
        return;
    }

    m_IsSearchGroupRunning = true;
    SearchEditorModelPlus::searchEntry * search_entry = search_entries.first();
    LoadSearch(search_entry);
    ReplaceCurrent();
    m_IsSearchGroupRunning = false;
}

void FindReplacePlus::ReplaceSearch()
{
    // these entries are owned by the Search Editor who will clean up as needed
    QList<SearchEditorModelPlus::searchEntry*> search_entries = m_MainWindow->SearchEditorGetCurrentEntriesPlus();

    if (search_entries.isEmpty()) {
        emit AskWhyGetEmptyEntries();
        return;
    }

    SetKeyModifiers();
    m_IsSearchGroupRunning = true;

    foreach(SearchEditorModelPlus::searchEntry * search_entry, search_entries) {
        LoadSearch(search_entry);
        if (Replace()) {
            break;
        } else {
            m_MainWindow->SearchEditorRecordEntryAsCompletedPlus(search_entry);
        }
    }
    m_IsSearchGroupRunning = false;
    ResetKeyModifiers();
}

void FindReplacePlus::CountAllSearch()
{
    // these entries are owned by the Search Editor who will clean up as needed
    QList<SearchEditorModelPlus::searchEntry*> search_entries = m_MainWindow->SearchEditorGetCurrentEntriesPlus();

    if (search_entries.isEmpty()) {
        emit AskWhyGetEmptyEntries();
        return;
    }

    SetKeyModifiers();
    m_IsSearchGroupRunning = true;
    int count = 0;
    foreach(SearchEditorModelPlus::searchEntry * search_entry, search_entries) {
        LoadSearch(search_entry);
        count += Count();
    }
    m_IsSearchGroupRunning = false;

    if (count == 0) {
        CannotFindSearchTerm();
    } else if (count > 0) {
        QString message = tr("Matches found: %n", "", count);
        ShowMessage(message);
    }
    ResetKeyModifiers();
}


void FindReplacePlus::CountsReportCount(SearchEditorModelPlus::searchEntry* entry, int& count)
{
    if (entry) {
        SetKeyModifiers();
        m_IsSearchGroupRunning = true;
        LoadSearch(entry);
        count = Count();
        m_IsSearchGroupRunning = false;
    } else {
        qDebug() << "why am I here";
        count = -1;
    }
}


void FindReplacePlus::ReplaceAllSearch()
{
    // these entries are owned by the Search Editor who will clean up as needed
    QList<SearchEditorModelPlus::searchEntry*> search_entries = m_MainWindow->SearchEditorGetCurrentEntriesPlus();

    if (search_entries.isEmpty()) {
        emit AskWhyGetEmptyEntries();
        return;
    }

    SetKeyModifiers();
    m_IsSearchGroupRunning = true;
    int count = 0;
    foreach(SearchEditorModelPlus::searchEntry * search_entry, search_entries) {
        LoadSearch(search_entry);
        count += ReplaceAll();
        m_MainWindow->SearchEditorRecordEntryAsCompletedPlus(search_entry);
    }
    m_IsSearchGroupRunning = false;

    if (count == 0) {
        ShowMessage(tr("No replacements made"));
    } else {
        QString message = tr("Replacements made: %n", "", count);
        ShowMessage(message);
    }
    ResetKeyModifiers();
}

void FindReplacePlus::TokeniseSelection()
{
    PasteTargetComboBox* cbFind = nullptr;
    if (ui.cbFind->hasFocus()) {
        cbFind = qobject_cast<PasteTargetComboBox*>(ui.cbFind);
        if (!IsValidFindText()) return;
    }
    else if (ui.cbPreFind->hasFocus()) {
        cbFind = qobject_cast<PasteTargetComboBox*>(ui.cbPreFind);
        if (GetSearchRegex().isEmpty()) return;
    }

    QString text;

    if (cbFind->lineEdit()->hasSelectedText()) {
        // We want to tokenise only the selection
        text = cbFind->lineEdit()->selectedText();
    }
    else {
        // We will tokenise the whole thing
        text = cbFind->lineEdit()->text();
    }

    QString new_text = TokeniseForRegex(text, true);

    if (new_text != text) {
        if (cbFind->lineEdit()->hasSelectedText()) {
            // We will paste in the new text so the user has the ability to undo.
            cbFind->PasteText(new_text);
        }
        else {
            // We still want to paste in, but we replacing all the text that is in there
            cbFind->lineEdit()->selectAll();
            cbFind->PasteText(new_text);
        }
    }
}

QString FindReplacePlus::TokeniseForRegex(const QString& text, bool includeNumerics)
{
    QString new_text(text);

    // Convert any form of newline or tabs to multiple spaces
    new_text.replace(QRegularExpression("\\R"), "  ");
    new_text.replace("\\t", "  ");

    // If the text does not contain a backslash we "assume" it has not been
    // tokenised already so we need to escape it
    if (!new_text.contains("\\")) {
        new_text = QRegularExpression::escape(new_text);
    }

    // Restore some characters for readability
    new_text.replace("\\ ", " ");
    new_text.replace("\\<", "<");
    new_text.replace("\\>", ">");
    new_text.replace("\\/", "/");
    new_text.replace("\\;", ";");
    new_text.replace("\\:", ":");
    new_text.replace("\\&", "&");
    new_text.replace("\\=", "=");

    // Replace multiple spaces
    new_text.replace(QRegularExpression("(\\s{2,})"), "\\s+");

    if (includeNumerics) {
        // Replace numerics.
        new_text.replace(QRegularExpression("(\\d+)"), "\\d+");
    }

    return new_text;
}

void FindReplacePlus::SetSearchMode(int search_mode)
{
    ui.cbSearchMode->setCurrentIndex(0);

    for (int i = 0; i < ui.cbSearchMode->count(); i++) {
        if (ui.cbSearchMode->itemData(i) == search_mode) {
            ui.cbSearchMode->setCurrentIndex(i);
            break;
        }
    }
}

void FindReplacePlus::SetLookWhere(int look_where)
{
    ui.cbLookWhere->setCurrentIndex(0);

    for (int i = 0; i < ui.cbLookWhere->count(); i++) {
        if (ui.cbLookWhere->itemData(i)  == look_where) {
            ui.cbLookWhere->setCurrentIndex(i);
            break;
        }
    }
}

void FindReplacePlus::SetSearchDirection(int search_direction)
{
    ui.cbSearchDirection->setCurrentIndex(0);

    for (int i = 0; i < ui.cbSearchDirection->count(); i++) {
        if (ui.cbSearchDirection->itemData(i) == search_direction) {
            ui.cbSearchDirection->setCurrentIndex(i);
            break;
        }
    }
}

void FindReplacePlus::ClearHistory()
{
    QMessageBox::StandardButton button_pressed;
    button_pressed = Utility::warning(this,
            tr("Sigil"),
            tr("Are you sure you want to clear your Find and Replace current values and history?"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel
            );

    if (button_pressed == QMessageBox::Yes) {
        ui.cbPreFind->clear();
        ui.cbFind->clear();
        ui.cbReplace->clear();
    }
}

void FindReplacePlus::SetOptionWrap(bool new_state)
{
    m_OptionWrap = new_state;
    ui.chkOptionWrap->setChecked(new_state);
}

void FindReplacePlus::SetOptionPreview(bool new_state)
{
    m_OptionPreview = new_state;
    ui.chkOptionPreview->setChecked(new_state);
}

void FindReplacePlus::SetFRButtonsTextOnly() {
    // Set F&R Buttons to text-only
    QList<QToolButton *> all_toolbuttons = findChildren<QToolButton *>();
    foreach(QToolButton * toolbutton, all_toolbuttons) {
        // tbRegexOptions QToolButton is always text-only
        // Close button is always icon-only
        if (toolbutton->objectName() != "tbRegexOptions" && toolbutton->objectName() != "close") {
            toolbutton->setToolButtonStyle(Qt::ToolButtonTextOnly);
        }
    }
}

// The UI is setup based on the capabilities.
void FindReplacePlus::ExtendUI()
{

    QString tooltip;
    tooltip = "<p style=\"padding-top:0.5em;\"><b>" + tr("Find") + "</b></p>"
              "<p style=\"margin-left: 0.5em;\">" + tr("Find next match.") + "</p>";
    ui.findNext->setToolTip(tooltip);
    tooltip = "<p style=\"padding-top:0.5em;\"><b>" + tr("Replace/Find") + "</b></p>"
              "<p style=\"margin-left: 0.5em;\">" + tr("Replace highlighted match (if any), then find the Next match in Code View.") + "</p>";
    ui.replaceFind->setToolTip(tooltip);
    tooltip = "<p style=\"padding-top:0.5em;\"><b>" + tr("Replace") + "</b></p>"
              "<p style=\"margin-left: 0.5em;\">" + tr("Replace highlighted match (if any) in Code View.") + "</p>";
    ui.replaceCurrent->setToolTip(tooltip);
    tooltip = "<p style=\"padding-top:0.5em;\"><b>" + tr("Replace All") + "</b></p>"
              "<p style=\"margin-left: 0.5em;\">" + tr("Replace all matches in Code View.") + "</p>"
              "<p style=\"margin-left: 0.5em;\">" + tr("Use with SHIFT to Filter Replacements before changes are applied.") + "</p>";
    ui.replaceAll->setToolTip(tooltip);
    tooltip = "<p style=\"padding-top:0.5em;\"><b>" + tr("Count All") + "</b></p>"
              "<p style=\"margin-left: 0.5em;\">" + tr("Count all matches in Code View.") + "</p>"
              "<p style=\"margin-left: 0.5em;\">" + tr("Use with SHIFT to generate a Dry Run Replace All table.") + "</p>";
    ui.count->setToolTip(tooltip);

    // Clear these because we want to add their items based on the
    // capabilities.
    ui.cbSearchMode->clear();
    ui.cbLookWhere->clear();
    ui.cbSearchDirection->clear();

    QString mode_tooltip = "<dl>";
    ui.cbSearchMode->addItem(tr("Normal"), FindReplacePlus::SearchMode_Normal);
    mode_tooltip += "<dt><b>" + tr("Normal") + "</b><dd>" + tr("Case in-sensitive search of exactly what you type.") + "</dd>";

    ui.cbSearchMode->addItem(tr("Regex"), FindReplacePlus::SearchMode_Regex);
    mode_tooltip += "<dt><b>" + tr("Regex") + "</b><dd>" + tr("Search for a pattern using Regular Expression syntax.") + "</dd>";

    ui.cbSearchMode->addItem(tr("PreSearch Regex"), FindReplacePlus::SearchMode_PreSearch);
    mode_tooltip += "<dt><b>" + tr("Regex With PreSearch") + "</b><dd>" + tr("Search and replace based on the content which searched by Pre Search Regex Expression.") + "</dd>";

    ui.cbSearchMode->setToolTip(mode_tooltip);

    QString look_tooltip = "<dl>";

    ui.cbLookWhere->addItem(tr("Current File"), FindReplacePlus::LookWhere_CurrentFile);
    look_tooltip += "<dt><b>" + tr("Current File") + "</b><dd>" + tr("Restrict the find or replace to the opened file.  Hold the Ctrl key down while clicking any search buttons to temporarily restrict the search to the Current File.") + "</dd>";

    ui.cbLookWhere->addItem(tr("All HTML Files"), FindReplacePlus::LookWhere_AllHTMLFiles);
    look_tooltip += "<dt><b>" + tr("All HTML Files") + "</b><dd>" + tr("Find or replace in all HTML files in Code View.") + "</dd>";

    ui.cbLookWhere->addItem(tr("All CSS Files"), FindReplacePlus::LookWhere_AllCSSFiles);
    look_tooltip += "<dt><b>" + tr("All CSS Files") + "</b><dd>" + tr("Find or replace in all CSS files in Code View.") + "</dd>";

    ui.cbLookWhere->addItem(tr("Selected Files"), FindReplacePlus::LookWhere_SelectedFiles);
    look_tooltip += "<dt><b>" + tr("Selected Files") + "</b><dd>" + tr("Restrict the find or replace to the files selected in the Book Browser in Code View.") + "</dd>";

    ui.cbLookWhere->addItem(tr("OPF File"), FindReplacePlus::LookWhere_OPFFile);
    look_tooltip += "<dt><b>" + tr("OPF File") + "</b><dd>" + tr("Restrict the find or replace to the OPF file.") + "</dd>";

    ui.cbLookWhere->addItem(tr("NCX File"), FindReplacePlus::LookWhere_NCXFile);
    look_tooltip += "<dt><b>" + tr("NCX File") + "</b><dd>" + tr("Restrict the find or replace to the NCX file.") + "</dd>";

    look_tooltip += "</dl>";
    ui.cbLookWhere->setToolTip(look_tooltip);

    // Special Marked Text indicator.
    QString mark_tooltip = "<dl>";
    ui.MarkedTextIndicator->addItem(tr("Marked Text"));
    mark_tooltip += "<dt><b>" + tr("Marked Text") + "</b><dd>" + tr("Restrict the find or replace to the text marked by Search Mark Selected Text. Cleared if you use Undo, enter text, or change views or tabs.") + "</dd>";
    mark_tooltip += "</dl>";
    ui.MarkedTextIndicator->setToolTip(mark_tooltip);

    ui.cbSearchDirection->addItem(tr("Up"), FindReplacePlus::Direction_Up);
    ui.cbSearchDirection->addItem(tr("Down"), FindReplacePlus::Direction_Down);
    ui.cbSearchDirection->setToolTip("<dl>"
                                     "<dt><b>" + tr("Up") + "</b><dd>" + tr("Search for the previous match from your current position.") + "</dd>"
                                     "<dt><b>" + tr("Down") + "</b><dd>" + tr("Search for the next match from your current position.") + "</dd>"
                                     "</dl>");
}


void FindReplacePlus::ValidateRegex()
{
    if (GetSearchMode() != FindReplacePlus::SearchMode_Normal) {
        QString rawtext = ui.cbFind->lineEdit()->text();
        QString text = GetSearchRegex();
        int offset_correction = text.length() - rawtext.length();
        SPCRE rex(text);
        QString emsg;
        if (!rex.isValid()) {
            emsg = tr("Invalid Regex:") + " " + PCREErrors::instance()->GetError(rex.getError(), "");
            emsg = emsg + "\n" + tr("offset:") + " " + QString::number(rex.getErrPos() - offset_correction);
            ui.cbFind->setToolTip(emsg);
            ui.actionReInvalidIcon->setToolTip(emsg);
            ui.cbFind->lineEdit()->addAction(ui.actionReInvalidIcon, QLineEdit::TrailingPosition);
        }
        else {
            ui.cbFind->setToolTip(tr("Valid Regex"));
            ui.cbFind->lineEdit()->removeAction(ui.actionReInvalidIcon);
        }
        return;
    }
    ui.cbFind->setToolTip("");
}

void FindReplacePlus::ValidatePreRegex()
{
    if (GetSearchMode() == FindReplacePlus::SearchMode_PreSearch) {
        QString rawtext = ui.cbPreFind->lineEdit()->text();
        QString text = GetPreSearchRegex();
        int offset_correction = text.length() - rawtext.length();
        SPCRE rex(text);
        QString emsg;
        if (!rex.isValid()) {
            emsg = tr("Invalid Regex:") + " " + PCREErrors::instance()->GetError(rex.getError(), "");
            emsg = emsg + "\n" + tr("offset:") + " " + QString::number(rex.getErrPos() - offset_correction);
            ui.cbPreFind->setToolTip(emsg);
            ui.actionReInvalidIcon->setToolTip(emsg);
            ui.cbPreFind->lineEdit()->addAction(ui.actionReInvalidIcon, QLineEdit::TrailingPosition);
        }
        else {
            ui.cbPreFind->setToolTip(tr("Valid Regex"));
            ui.cbPreFind->lineEdit()->removeAction(ui.actionReInvalidIcon);
        }
    }
}

void FindReplacePlus::UpdatePreSearchUI()
{
    SearchMode mode = static_cast<SearchMode>(ui.cbSearchMode->currentIndex());
    if (mode == SearchMode::SearchMode_PreSearch) {
        ui.prefindl->setVisible(true);
        ui.cbPreFind->setVisible(true);
    }
    else {
        ui.prefindl->setVisible(false);
        ui.cbPreFind->setVisible(false);
    }
}

void FindReplacePlus::warningEmptyExpression()
{
    Utility::warning(this, tr("Find and Replace"), tr("Find Expression cannot be empty!"), QMessageBox::Ok);
}

void FindReplacePlus::warningEndedSearch()
{
    Utility::warning(this, tr("Find and Replace"),
        tr("The search has ended.Please click \"OK\" to restart the search."),
        QMessageBox::Ok);
}

void FindReplacePlus::ConnectSignalsToSlots()
{
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(expireMessage()));
    connect(ui.findNext, SIGNAL(clicked()), this, SLOT(FindClicked()));
    connect(ui.count, SIGNAL(clicked()), this, SLOT(CountClicked()));
    connect(ui.replaceCurrent, SIGNAL(clicked()), this, SLOT(ReplaceCurrent()));
    connect(ui.replaceFind, SIGNAL(clicked()), this, SLOT(ReplaceClicked()));
    connect(ui.replaceAll, SIGNAL(clicked()), this, SLOT(ReplaceAllClicked()));
    connect(ui.close, SIGNAL(clicked()), this, SLOT(HideFindReplace()));
    connect(ui.chkOptionWrap, SIGNAL(clicked(bool)), this, SLOT(SetOptionWrap(bool)));
    connect(ui.chkOptionWrap, SIGNAL(clicked(bool)), this, SLOT(DoRestart()));
    connect(ui.chkOptionPreview, SIGNAL(clicked(bool)), this, SLOT(SetOptionPreview(bool)));
    connect(ui.cbFind, SIGNAL(editTextChanged(const QString&)), this, SLOT(ValidateRegex()));
    connect(ui.cbFind, SIGNAL(currentTextChanged(const QString&)), this, SLOT(ValidateRegex()));
    connect(ui.cbPreFind, SIGNAL(editTextChanged(const QString&)), this, SLOT(ValidatePreRegex()));
    connect(ui.cbPreFind, SIGNAL(currentTextChanged(const QString&)), this, SLOT(ValidatePreRegex()));
    connect(ui.cbSearchMode, SIGNAL(currentTextChanged(const QString&)), this, SLOT(ValidateRegex()));
    connect(ui.cbSearchMode, SIGNAL(currentTextChanged(const QString&)), this, SLOT(ValidatePreRegex()));
    connect(ui.cbSearchMode, SIGNAL(currentTextChanged(const QString&)), this, SLOT(UpdatePreSearchUI()));
}
