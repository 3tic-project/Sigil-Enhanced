/*******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Kevin B. Hendricks, Stratford, ON, Canada
 *   
 *  Based on wojtodzio/ImageViewer from github with lots of bug fixes
 *      and improvements added, and modified to be a QWidget to work
 *      inside Sigil.
 * 
 *      Original code was: Copyright (c) 2016 Wojciech Wrona 
 *                         with this MIT License
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <algorithm>
#include <cmath>
#include <QTransform>
#include <QDebug>
#include <QFileInfo>
#include <QFile>
#include <QLocale>
#include <QBuffer>
#include <QImageWriter>
#include <QInputDialog>
#include <QKeySequence>
#include <QWheelEvent>
#include <QTemporaryFile>
#include "Misc/AtomicFileWrite.h"
#include "Misc/SettingsStore.h"
#include "Misc/Utility.h"
#include "Misc/WebpSupport.h"
#include "EmbedPython/PythonRoutines.h"
#include "Dialogs/ImageResizeDialog.h"
#include "Widgets/BetterRubberBand.h"
#include "Widgets/AdjustImage.h"
#include "ui_AdjustImage.h"

static const QString SETTINGS_GROUP = "adjust_image";
static QStringList SAVE_QUALITY_MEDIATYPES = QStringList() << "image/jpeg" << "image/webp" << "image/avif" << "image/jxl";
static const int MAX_IMAGE_HISTORY_STEPS = 20;
static const double MIN_IMAGE_ZOOM = 0.10;
static const double MAX_IMAGE_ZOOM = 3.0;
static const double IMAGE_ZOOM_STEP_IN = 1.25;
static const double IMAGE_ZOOM_STEP_OUT = 0.80;

static double clampImageZoom(double factor)
{
    return std::clamp(factor, MIN_IMAGE_ZOOM, MAX_IMAGE_ZOOM);
}

AdjustImage::AdjustImage(const QString filepath, const QString& mediatype,  QWidget *parent) :
    QWidget(parent),
    ui(new Ui::AdjustImage),
    m_mediatype(mediatype),
    m_lastPos(QPoint(0,0))
{
    ui->setupUi(this);
    m_mainToolBar = ui->mainToolBar;
    m_statusBar = ui->statusBar;

    updateActions(false);
    ui->actionUndo->setEnabled(false);
    ui->actionRedo->setEnabled(false);

    m_imageLabel = new QLabel;
    m_imageLabel->resize(0, 0);
    m_imageLabel->setMouseTracking(true);
    m_imageLabel->setBackgroundRole(QPalette::Base);
    m_imageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_imageLabel->setScaledContents(true);
    m_imageLabel->installEventFilter(this);
    // our rubber band must be a child of the m_imageLabel
    // otherwise there is a coordinate nightmare
    m_rb = new BetterRubberBand(QRubberBand::Rectangle, m_imageLabel);
    m_rb->hide();

    m_scrollArea = new QScrollArea;
    m_scrollArea->setBackgroundRole(QPalette::Dark);
    m_scrollArea->setWidget(m_imageLabel);
    m_scrollArea->viewport()->installEventFilter(this);

    m_description = new QLabel;
    m_statusBar->addPermanentWidget(m_description);
    // update tooltips on toolbar icons to include shortcut in a platform specific manner
    extendToolTip(ui->actionSave,        "Ctrl+S");    
    extendToolTip(ui->actionZoomIn,      "Ctrl++");
    extendToolTip(ui->actionZoomOut,     "Ctrl+-");
    extendToolTip(ui->actionZoomToFit,   "Ctrl+F");
    extendToolTip(ui->actionUndo,        "Ctrl+Z");
    extendToolTip(ui->actionRedo,        "Ctrl+Y");
    extendToolTip(ui->actionRotateLeft,  "Ctrl+L");
    extendToolTip(ui->actionRotateRight, "Ctrl+R");
    extendToolTip(ui->actionCrop,        "Ctrl+K");
    extendToolTip(ui->actionResizeImage, "Ctrl+E");
        
    vlayout = new QVBoxLayout;
    vlayout->setContentsMargins(2,2,2,2);
    vlayout->addWidget(m_mainToolBar);
    vlayout->addWidget(m_scrollArea);
    vlayout->addWidget(m_statusBar);
    setLayout(vlayout);

    setWindowTitle(tr("Adjust Image"));
    if (!filepath.isEmpty()) {
        m_fileName = filepath;
        m_ffsize = QFile(m_fileName).size() / 1024.0;
        m_fsize =  QLocale().toString(m_ffsize, 'f', 2);
        QString load_error;
        m_image = LoadRasterImage(m_fileName, &load_error);
        if (m_image.isNull()) {
             QMessageBox::information(this,
                                      tr("Adjust Image"),
                                      load_error.isEmpty()
                                          ? tr("Cannot load %1.").arg(m_fileName)
                                          : load_error);
             return;
        }
        m_scaleFactor = 1.0;
        m_croppingState = false;
        setCursor(Qt::ArrowCursor);
        updateActions(true);
        refreshLabel();
    }
    ConnectSignalsToSlots();
    ReadSettings();
}

AdjustImage::~AdjustImage()
{
    WriteSettings();
    m_history.clear();
    m_reverseHistory.clear();
    delete ui;
}

bool AdjustImage::isCropEnabled() { return ui->actionCrop->isEnabled(); }  
bool AdjustImage::isUndoEnabled() { return ui->actionUndo->isEnabled(); }
bool AdjustImage::isRedoEnabled() { return ui->actionRedo->isEnabled(); }

void AdjustImage::updateUndoRedoActions()
{
    bool undo_enabled = !m_history.isEmpty();
    bool redo_enabled = !m_reverseHistory.isEmpty();
    bool changed = ui->actionUndo->isEnabled() != undo_enabled ||
                   ui->actionRedo->isEnabled() != redo_enabled;

    ui->actionUndo->setEnabled(undo_enabled);
    ui->actionRedo->setEnabled(redo_enabled);

    if (changed) {
        emit UndoRedoStateChanged();
    }
}


void AdjustImage::extendToolTip(QAction*m, const QString sc)
{
    QString shct = QKeySequence(sc).toString(QKeySequence::NativeText);
    QString current_tip = m->toolTip();
    m->setToolTip(current_tip + " (" + shct + ")");
}

void AdjustImage::ReadSettings()
{
    SettingsStore settings;
    settings.beginGroup(SETTINGS_GROUP);
    m_jpeg_quality = settings.value("jpeg_quality", QVariant(93)).toInt();
    m_webp_quality = settings.value("webp_quality", QVariant(90)).toInt();
    m_jxl_quality =  settings.value("jxl_quality", QVariant(93)).toInt();
    m_avif_quality = settings.value("avif_quality", QVariant(90)).toInt();
    settings.endGroup();
}


void AdjustImage::WriteSettings()
{
    SettingsStore settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue("jpeg_quality", m_jpeg_quality);
    settings.setValue("webp_quality", m_webp_quality);
    settings.setValue("jxl_quality",  m_jxl_quality);
    settings.setValue("avif_quality", m_avif_quality);
    settings.endGroup();
}

QRect AdjustImage::BuildRect(const QPoint& p1, const QPoint& p2)
{
    QRect arect = QRect(p1, p2).normalized();
    if ((arect.x() < 2) && (arect.y() < 2)) {
        arect.setX(0);
        arect.setY(0);
    }
    return arect;
}


void AdjustImage::UpdateImageDescription()
{
    QString colors_shades = m_image.isGrayscale() ? tr("shades") : tr("colors");
    QString grayscale_color = m_image.isGrayscale() ? tr("Grayscale") : tr("Color");
    QString colorsInfo = "";
    if (m_image.depth() == 32) {
        colorsInfo = QString(" %1bpp").arg(m_image.bitPlaneCount());
    } else if (m_image.depth() > 0) {
        colorsInfo = QString(" %1bpp (%2 %3)").arg(m_image.bitPlaneCount()).arg(m_image.colorCount()).arg(colors_shades);
    }
    QString description = QString("(%1px × %2px) %3 KB  %4%5").arg(m_image.width()).arg(m_image.height()).arg(m_fsize).arg(grayscale_color).arg(colorsInfo);
    m_description->setText(description);
}

void AdjustImage::adjustScrollBar(QScrollBar *scrollBar, double factor)
{
    int newValue = factor * scrollBar->value() + (factor - 1) * scrollBar->pageStep() / 2;
    scrollBar->setValue(newValue);
}

void AdjustImage::changeCroppingState(bool changeTo)
{
    m_croppingState = changeTo;
    ui->actionCrop->setDisabled(changeTo);

    if (changeTo) {
        updateActions(false);
        
        m_rb->setGeometry(0, 0, ((m_image.width() * m_scaleFactor)/2), ((m_image.height() * m_scaleFactor)/2));
        m_rb->show();
        // a focus policy that accepts keyboard focus is required for setFocus() to work
        setFocusPolicy(Qt::StrongFocus);
        setFocus();
        QString msg = tr("Crop Mode: Enter to Crop, Escape to Abort");
        m_statusBar->showMessage(msg);
    } else {
        m_rb->hide();
        updateActions(true);
        QString msg = tr("Exiting Crop Mode");
        m_statusBar->showMessage(msg);
    }
}

void AdjustImage::refreshLabel()
{
    m_imageLabel->setPixmap(QPixmap::fromImage(m_image));
    UpdateImageDescription();
    scaleImageBy(1.0);
}

void AdjustImage::rotateImage(int angle)
{
    saveToHistoryWithClear(m_image);
    QPixmap pixmap(m_imageLabel->pixmap());
    QTransform rm;
    rm.rotate(angle);
    pixmap = pixmap.transformed(rm, Qt::SmoothTransformation);
    m_image = pixmap.toImage();
    refreshLabel();
}

void AdjustImage::appendToLimitedHistory(QVector<QImage> &history, const QImage &imageToSave)
{
    history.push_back(imageToSave);
    if (history.size() > MAX_IMAGE_HISTORY_STEPS) {
        history.remove(0, history.size() - MAX_IMAGE_HISTORY_STEPS);
    }
}

void AdjustImage::saveToHistory(QImage imageToSave)
{
    appendToLimitedHistory(m_history, imageToSave);
    updateUndoRedoActions();
}

void AdjustImage::saveToHistoryWithClear(QImage imageToSave)
{
    appendToLimitedHistory(m_history, imageToSave);
    m_reverseHistory.clear();
    updateUndoRedoActions();
}

void AdjustImage::saveToReverseHistory(QImage imageToSave)
{
    appendToLimitedHistory(m_reverseHistory, imageToSave);
    updateUndoRedoActions();
}

void AdjustImage::resizeImage(int targetW, int targetH)
{
    // no size change so don't record a history step or rescale the image
    if (targetW == m_image.width() && targetH == m_image.height()) {
        return;
    }

    saveToHistoryWithClear(m_image);
    QPixmap pixmap(m_imageLabel->pixmap());
    pixmap = pixmap.scaled(targetW, targetH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    m_image = pixmap.toImage();
    refreshLabel();
}

void AdjustImage::scaleImageBy(double factor)
{
    double old_scale_factor = m_scaleFactor;
    m_scaleFactor = clampImageZoom(m_scaleFactor * factor);
    m_imageLabel->resize(m_scaleFactor * m_imageLabel->pixmap().size());

    double scroll_factor = old_scale_factor > 0 ? m_scaleFactor / old_scale_factor : 1.0;
    adjustScrollBar(m_scrollArea->horizontalScrollBar(), scroll_factor);
    adjustScrollBar(m_scrollArea->verticalScrollBar(), scroll_factor);

    updateZoomActions();
    emit InternalZoomFactorChanged(m_scaleFactor);
}

void AdjustImage::scaleImageUsing(double factor)
{
    double old_scale_factor = m_scaleFactor;
    m_scaleFactor = clampImageZoom(factor);
    m_imageLabel->resize(m_scaleFactor * m_imageLabel->pixmap().size());

    double scroll_factor = old_scale_factor > 0 ? m_scaleFactor / old_scale_factor : 1.0;
    adjustScrollBar(m_scrollArea->horizontalScrollBar(), scroll_factor);
    adjustScrollBar(m_scrollArea->verticalScrollBar(), scroll_factor);

    updateZoomActions();
    UpdateZoomedCoordinates();
}

void AdjustImage::updateZoomActions()
{
    ui->actionZoomIn->setEnabled(m_scaleFactor < MAX_IMAGE_ZOOM);
    ui->actionZoomOut->setEnabled(m_scaleFactor > MIN_IMAGE_ZOOM);
}

void AdjustImage::updateActions(bool updateTo)
{
    ui->actionCrop->setEnabled(updateTo);
    ui->actionResizeImage->setEnabled(updateTo);
    ui->actionRotateLeft->setEnabled(updateTo);
    ui->actionRotateRight->setEnabled(updateTo);
    ui->actionSave->setEnabled(updateTo);
    if (updateTo) {
        updateZoomActions();
    } else {
        ui->actionZoomIn->setEnabled(false);
        ui->actionZoomOut->setEnabled(false);
    }
    ui->actionZoomToFit->setEnabled(updateTo);
}


void AdjustImage::keyPressEvent(QKeyEvent *event)
{
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_S) {
        doSave();
        event->accept();
        return;
    }
    if (!m_croppingState) {
        QWidget::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        m_rb->hide();
        refreshLabel();
        changeCroppingState(false);
        updateActions(true);
    } else if ((event->key() == Qt::Key_Return) || (event->key() == Qt::Key_Enter)) {
        saveToHistoryWithClear(m_image);
        m_croppingStart = m_rb->getTopLeftPos() / m_scaleFactor;
        m_croppingEnd = m_rb->getBottomRightPos() / m_scaleFactor;
        m_rb->hide();
        QRect rect = BuildRect(m_croppingStart, m_croppingEnd);
        m_image = m_image.copy(rect);
        refreshLabel();
        changeCroppingState(false);
    } else {
        QWidget::keyPressEvent(event);
    }
}

void AdjustImage::UpdateZoomedCoordinates()
{
    QString sf = QString::number(m_scaleFactor, 'f', 4);
    QString msg = tr("(x,y) coordinates:") + " (%1,%2)  " + tr("Zoom") + " (%3)";
    int x_pos = std::round(m_lastPos.x() / m_scaleFactor);
    int y_pos = std::round(m_lastPos.y()/ m_scaleFactor);
    msg = msg.arg(x_pos).arg(y_pos).arg(sf);
    m_statusBar->showMessage(msg);
}


// Slots

bool AdjustImage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_imageLabel && watched != m_scrollArea->viewport())
        return false;

    if (event->type() == QEvent::Wheel) {
        QWheelEvent* const we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            const QPoint delta = !we->angleDelta().isNull() ? we->angleDelta() : we->pixelDelta();

            if (delta.y() > 0) {
                scaleImageBy(IMAGE_ZOOM_STEP_IN);
                event->accept();
                return true;
            }
            if (delta.y() < 0) {
                scaleImageBy(IMAGE_ZOOM_STEP_OUT);
                event->accept();
                return true;
            }
        }
        return false;
    }

    if (watched != m_imageLabel)
        return false;

    switch (event->type())
    {
        case QEvent::MouseMove:
        {
            const QMouseEvent* const me = static_cast<const QMouseEvent*>(event);
            const QPoint position = me->pos();
            m_lastPos = position;
            UpdateZoomedCoordinates();
            break;
        }

        default:
            break;
    }
    return QObject::eventFilter(watched, event);
}

void AdjustImage::doCrop()
{
    changeCroppingState(true);
}

void AdjustImage::doResizeImage()
{
    // only record history once the resize is actually applied in resizeImage()
    // so canceling the dialog leaves the undo/redo stacks untouched
    int width = m_image.width();
    int height = m_image.height();
    ImageResizeDialog dlg(width, height, this);
    if (dlg.exec() == QDialog::Accepted) {
        int newWidth = dlg.getWidth();
        int newHeight = dlg.getHeight();
        resizeImage(newWidth, newHeight);
    }
}


void AdjustImage::doRotateLeft()
{
    rotateImage(-90);
}

void AdjustImage::doRotateRight()
{
    rotateImage(90);
}

static bool EncodeEditedImage(const QImage &image, const QString &format,
                              const QString &fileName, int quality,
                              QByteArray *bytes, QString *error)
{
    auto fail = [error](const QString &text) {
        if (error) {
            *error = text;
        }
        return false;
    };
    if (format == QLatin1String("GIF")) {
        // Qt can read but not write even static GIF files.
        // Encode to a temp GIF, then the caller replaces the book file.
        // Writing the GIF straight onto the extracted path fails on Windows
        // when that path is still open, and a failed write used to leave
        // the book unmarked.
        const QString targetDir = Utility::DefinePrefsDir() + "/workspace";
        QTemporaryFile png(targetDir + "/XXXXXX.png");
        png.setAutoRemove(true);
        if (!png.open() || !image.save(&png, "PNG", -1)) {
            return fail(AdjustImage::tr("Image save failed."));
        }
        const QString pngPath = png.fileName();
        png.close();
        QTemporaryFile gif(targetDir + "/XXXXXX.gif");
        gif.setAutoRemove(true);
        if (!gif.open()) {
            return fail(AdjustImage::tr("Image save failed."));
        }
        const QString gifPath = gif.fileName();
        gif.close();
        PythonRoutines pr;
        if (!pr.ConvertPngToGifInPython(pngPath, gifPath)) {
            return fail(AdjustImage::tr("GIF conversion failed."));
        }
        QFile gifFile(gifPath);
        if (!gifFile.open(QIODevice::ReadOnly)) {
            return fail(gifFile.errorString());
        }
        *bytes = gifFile.readAll();
        if (bytes->isEmpty()) {
            return fail(AdjustImage::tr("GIF conversion failed."));
        }
        return true;
    }

    QByteArray writerFormat = format.toLatin1();
    if (writerFormat.isEmpty()) {
        writerFormat = QFileInfo(fileName).suffix().toLatin1();
    }
    QBuffer buffer(bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return fail(AdjustImage::tr("Image save failed."));
    }
    QImageWriter writer(&buffer, writerFormat);
    if (quality != -1) {
        writer.setQuality(quality);
    }
    writer.setOptimizedWrite(true);
    if (!writer.write(image)) {
        return fail(AdjustImage::tr("Image save failed: ") + writer.errorString());
    }
    if (bytes->isEmpty()) {
        return fail(AdjustImage::tr("Image save failed."));
    }
    return true;
}

void AdjustImage::doSave()
{
    QString format;
    if (m_mediatype.startsWith(QLatin1String("image/"))) {
        format = m_mediatype.mid(6).toUpper();
    }
    if (format == QLatin1String("PBM") || format == QLatin1String("PGM")) {
        m_statusBar->showMessage(tr("PBM and PGM Image formats can not be saved. Save aborted."));
        return;
    }

    int quality = -1;
    if (format != QLatin1String("GIF") && SAVE_QUALITY_MEDIATYPES.contains(m_mediatype)) {
        if (m_mediatype == QLatin1String("image/jpeg")) quality = m_jpeg_quality;
        if (m_mediatype == QLatin1String("image/webp")) quality = m_webp_quality;
        if (m_mediatype == QLatin1String("image/jxl"))  quality = m_jxl_quality;
        if (m_mediatype == QLatin1String("image/avif")) quality = m_avif_quality;
        bool ok = false;
        quality = QInputDialog::getInt(nullptr, tr("Image Quality"),
                                       tr("Enter quality level (0-100):"), quality, 0, 100, 1, &ok);
        if (!ok) {
            m_statusBar->showMessage(tr("Image save aborted, as quality unavailable."));
            return;
        }
        if (m_mediatype == QLatin1String("image/jpeg")) m_jpeg_quality = quality;
        if (m_mediatype == QLatin1String("image/webp")) m_webp_quality = quality;
        if (m_mediatype == QLatin1String("image/jxl"))  m_jxl_quality = quality;
        if (m_mediatype == QLatin1String("image/avif")) m_avif_quality = quality;
    }

    QByteArray bytes;
    QString error;
    if (!EncodeEditedImage(m_image, format, m_fileName, quality, &bytes, &error)) {
        m_statusBar->showMessage(error.isEmpty() ? tr("Image save failed.") : error);
        return;
    }

    const bool written = AtomicFile::WriteBytesReplacing(m_fileName, bytes, &error);
    m_LastSaveWroteLiveFile = written;
    m_UnwrittenPayload = written ? QByteArray() : bytes;
    const qint64 storedSize = written ? QFile(m_fileName).size() : static_cast<qint64>(bytes.size());
    m_ffsize = storedSize / 1024.0;
    m_fsize = QLocale().toString(m_ffsize, 'f', 2);
    UpdateImageDescription();
    if (written) {
        m_statusBar->showMessage(tr("Image successfully saved."));
    } else {
        m_statusBar->showMessage(
            tr("Image saved for the EPUB. The extracted file could not be replaced and will be packaged on save: ") + error);
    }
    emit SetImageContentModified();
    m_UnwrittenPayload.clear();
}


void AdjustImage::toggleShowToolbar(bool checked)
{
    if (checked)
        m_mainToolBar->show();
    else
        m_mainToolBar->hide();
}


void AdjustImage::doUndo()
{
    if (m_history.isEmpty()) {
        return;
    }
    appendToLimitedHistory(m_reverseHistory, m_image);
    m_image = m_history.last();
    refreshLabel();
    m_history.pop_back();
    updateUndoRedoActions();
}

void AdjustImage::doRedo()
{
    if (m_reverseHistory.isEmpty()) {
        return;
    }
    appendToLimitedHistory(m_history, m_image);
    m_image = m_reverseHistory.last();
    refreshLabel();
    m_reverseHistory.pop_back();
    updateUndoRedoActions();
}

void AdjustImage::doZoomIn()
{
    scaleImageBy(IMAGE_ZOOM_STEP_IN);
    UpdateZoomedCoordinates();
}

void AdjustImage::doZoomOut()
{
    scaleImageBy(IMAGE_ZOOM_STEP_OUT);
    UpdateZoomedCoordinates();
}

void AdjustImage::doZoomToFit()
{
    QSize windowSize = m_scrollArea->viewport()->size();
    QSize labelSize = m_imageLabel->pixmap().size();

    if (windowSize.isEmpty() || labelSize.isEmpty()) {
        return;
    }

    double imageRatio = double(labelSize.height()) / labelSize.width();
    double scaleTo;

    if (windowSize.width() * imageRatio > windowSize.height()) {
        scaleTo = double(windowSize.height()) / labelSize.height();
    } else {
        scaleTo = double(windowSize.width()) / labelSize.width();
    }
    double scaleBy = scaleTo / m_scaleFactor;
    scaleImageBy(scaleBy);
    UpdateZoomedCoordinates();
}


void AdjustImage::ConnectSignalsToSlots()
{
    connect(ui->actionCrop,        SIGNAL(triggered()), this, SLOT(doCrop()));
    connect(ui->actionResizeImage, SIGNAL(triggered()), this, SLOT(doResizeImage()));
    connect(ui->actionRotateLeft,  SIGNAL(triggered()), this, SLOT(doRotateLeft()));
    connect(ui->actionRotateRight, SIGNAL(triggered()), this, SLOT(doRotateRight()));
    connect(ui->actionSave,        SIGNAL(triggered()), this, SLOT(doSave()));
    connect(ui->actionZoomIn,      SIGNAL(triggered()), this, SLOT(doZoomIn()));
    connect(ui->actionZoomOut,     SIGNAL(triggered()), this, SLOT(doZoomOut()));
    connect(ui->actionZoomToFit,   SIGNAL(triggered()), this, SLOT(doZoomToFit()));
    connect(ui->actionRedo,        SIGNAL(triggered()), this, SLOT(doRedo()));
    connect(ui->actionUndo,        SIGNAL(triggered()), this, SLOT(doUndo()));
    connect(ui->actionShowToolbar, SIGNAL(triggered(bool)), this, SLOT(toggleShowToolbar(bool)));
}
