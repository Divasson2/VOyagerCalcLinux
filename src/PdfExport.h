// SPDX-License-Identifier: MIT
// Copyright (c) 2026 José González Divassón
//
// PdfExport.h - the program documentation as a PDF (A4 or US Letter) through QPdfWriter, laid out like the
// Mac app's ProgramDocumentPDF: cover with the model, firmware, steps and description; labels; registers and
// flags; matrices and complex; the listing (step, display, keys with key-cap glyphs on the 15C, comment);
// comments that no longer match a step. Every page has a header and "Page n of N". The two HP15C Simulator
// fonts (display by Torsten Manz, key caps by Eric Rechlin) are embedded in the PDF. No branding.
#pragma once
#include "ProgramDocument.h"
#include <QByteArray>
#include <QString>

namespace vc {

enum class Paper { A4, Letter };

/// Loads the two fonts from the program's resources (needs a QGuiApplication); false if a font is missing.
struct PdfFonts {
    QString display, keys;                      // family names, empty when not loaded
    bool hasDisplay() const { return !display.isEmpty(); }
    bool hasKeys() const { return !keys.isEmpty(); }
    static PdfFonts fromResources();
};

QByteArray renderProgramDocumentPdf(const ProgramDocumentContent &content, Paper paper = Paper::A4,
                                    const PdfFonts &fonts = PdfFonts::fromResources());

}  // namespace vc
