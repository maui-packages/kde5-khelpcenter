/*
 *  This file is part of the KDE Help Center
 *
 *  Copyright (C) 2002 Frerich Raabe (raabe@kde.org)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Steet, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "glossary.h"
#include "view.h"

#include <kapplication.h>
#include <kconfig.h>
#include <kdebug.h>
#include <kiconloader.h>
#include <klocale.h>
#include <kmainwindow.h>
#include <kprocess.h>
#include <kstandarddirs.h>
#include <kstatusbar.h>

#include <q3header.h>
//Added by qt3to4:
#include <Q3Frame>
#include <QTextStream>

#include <sys/stat.h>

using namespace KHC;

class SectionItem : public KListViewItem
{
	public:
		SectionItem( Q3ListViewItem *parent, const QString &text )
			: KListViewItem( parent, text )
		{
			setOpen( false );
		}
		
		virtual void setOpen( bool open )
		{
				KListViewItem::setOpen(open);
				
				setPixmap( 0, SmallIcon( QLatin1String( open ? "contents" : "contents2" ) ) );

		}
};

class EntryItem : public KListViewItem
{
	public:
		EntryItem( SectionItem *parent, const QString &term, const QString &id )
			: KListViewItem( parent, term ),
			m_id( id )
		{
		}

		QString id() const { return m_id; }
	
	private:
		QString m_id;
};

Glossary::Glossary( QWidget *parent ) : KListView( parent )
{
	m_initialized = false;

	connect( this, SIGNAL( clicked( Q3ListViewItem * ) ),
	         this, SLOT( treeItemSelected( Q3ListViewItem * ) ) );
	connect( this, SIGNAL( returnPressed( Q3ListViewItem * ) ),
	         this, SLOT( treeItemSelected( Q3ListViewItem * ) ) );
	
	setFrameStyle( Q3Frame::Panel | Q3Frame::Sunken );
	addColumn( QString::null );
	header()->hide();
	setAllColumnsShowFocus( true );
	setRootIsDecorated( true );

	m_byTopicItem = new KListViewItem( this, i18n( "By Topic" ) );
	m_byTopicItem->setPixmap( 0, SmallIcon( "help" ) );

	m_alphabItem = new KListViewItem( this, i18n( "Alphabetically" ) );
	m_alphabItem->setPixmap( 0, SmallIcon( "charset" ) );

	m_cacheFile = locateLocal( "cache", "help/glossary.xml" );

	m_sourceFile = View::View::langLookup( QLatin1String( "khelpcenter/glossary/index.docbook" ) );

	m_config = kapp->config();
	m_config->setGroup( "Glossary" );

}

void Glossary::show()
{
	if ( !m_initialized ) {
		if ( cacheStatus() == NeedRebuild )
			rebuildGlossaryCache();
		else
			buildGlossaryTree();
		m_initialized = true;
	}
	KListView::show();
}

Glossary::~Glossary()
{
	m_glossEntries.setAutoDelete( true );
	m_glossEntries.clear();
}

const GlossaryEntry &Glossary::entry( const QString &id ) const
{
	return *m_glossEntries[ id ];
}

Glossary::CacheStatus Glossary::cacheStatus() const
{
	if ( !QFile::exists( m_cacheFile ) ||
	     m_config->readPathEntry( "CachedGlossary" ) != m_sourceFile ||
	     m_config->readNumEntry( "CachedGlossaryTimestamp" ) != glossaryCTime() )
		return NeedRebuild;

	return CacheOk;
}

int Glossary::glossaryCTime() const
{
	struct stat stat_buf;
	stat( QFile::encodeName( m_sourceFile ).data(), &stat_buf );

	return stat_buf.st_ctime;
}

void Glossary::rebuildGlossaryCache()
{
	KMainWindow *mainWindow = dynamic_cast<KMainWindow *>( kapp->mainWidget() );
	Q_ASSERT( mainWindow );
	mainWindow->statusBar()->message( i18n( "Rebuilding cache..." ) );

	KProcess *meinproc = new KProcess;
	connect( meinproc, SIGNAL( processExited( KProcess * ) ),
	         this, SLOT( meinprocExited( KProcess * ) ) );

	*meinproc << locate( "exe", QLatin1String( "meinproc" ) );
	*meinproc << QLatin1String( "--output" ) << m_cacheFile;
	*meinproc << QLatin1String( "--stylesheet" )
	          << locate( "data", QLatin1String( "khelpcenter/glossary.xslt" ) );
	*meinproc << m_sourceFile;

	meinproc->start( KProcess::NotifyOnExit );
}

void Glossary::meinprocExited( KProcess *meinproc )
{
	delete meinproc;

	if ( !QFile::exists( m_cacheFile ) )
		return;

	m_config->writePathEntry( "CachedGlossary", m_sourceFile );
	m_config->writeEntry( "CachedGlossaryTimestamp", glossaryCTime() );
	m_config->sync();
	
	m_status = CacheOk;

	KMainWindow *mainWindow = dynamic_cast<KMainWindow *>( kapp->mainWidget() );
	Q_ASSERT( mainWindow );
	mainWindow->statusBar()->message( i18n( "Rebuilding cache... done." ), 2000 );

	buildGlossaryTree();
}

void Glossary::buildGlossaryTree()
{
	QFile cacheFile(m_cacheFile);
	if ( !cacheFile.open( QIODevice::ReadOnly ) )
		return;

	QDomDocument doc;
	if ( !doc.setContent( &cacheFile ) )
		return;

	QDomNodeList sectionNodes = doc.documentElement().elementsByTagName( QLatin1String( "section" ) );
	for ( int i = 0; i < sectionNodes.count(); i++ ) {
		QDomElement sectionElement = sectionNodes.item( i ).toElement();
		QString title = sectionElement.attribute( QLatin1String( "title" ) );
		SectionItem *topicSection = new SectionItem( m_byTopicItem, title );

		QDomNodeList entryNodes = sectionElement.elementsByTagName( QLatin1String( "entry" ) );
		for ( int j = 0; j < entryNodes.count(); j++ ) {
			QDomElement entryElement = entryNodes.item( j ).toElement();
			
			QString entryId = entryElement.attribute( QLatin1String( "id" ) );
			if ( entryId.isNull() )
				continue;
				
			QDomElement termElement = childElement( entryElement, QLatin1String( "term" ) );
			QString term = termElement.text().simplified();

			EntryItem *entry = new EntryItem(topicSection, term, entryId );
            m_idDict.insert( entryId, entry );

			SectionItem *alphabSection = 0L;
			for ( Q3ListViewItemIterator it( m_alphabItem ); it.current(); it++ )
				if ( it.current()->text( 0 ) == QString( term[ 0 ].upper() ) ) {
					alphabSection = static_cast<SectionItem *>( it.current() );
					break;
				}

			if ( !alphabSection )
				alphabSection = new SectionItem( m_alphabItem, QString( term[ 0 ].upper() ) );

			new EntryItem( alphabSection, term, entryId );

			QDomElement definitionElement = childElement( entryElement, QLatin1String( "definition" ) );
			QString definition = definitionElement.text().simplified();

			GlossaryEntryXRef::List seeAlso;

			QDomElement referencesElement = childElement( entryElement, QLatin1String( "references" ) );
			QDomNodeList referenceNodes = referencesElement.elementsByTagName( QLatin1String( "reference" ) );
			if ( referenceNodes.count() > 0 )
				for ( int k = 0; k < referenceNodes.count(); k++ ) {
					QDomElement referenceElement = referenceNodes.item( k ).toElement();

					QString term = referenceElement.attribute( QLatin1String( "term" ) );
					QString id = referenceElement.attribute( QLatin1String( "id" ) );
					
					seeAlso += GlossaryEntryXRef( term, id );
				}
			
			m_glossEntries.insert( entryId, new GlossaryEntry( term, definition, seeAlso ) );
		}
	}
}

void Glossary::treeItemSelected( Q3ListViewItem *item )
{
	if ( !item )
		return;

	if ( EntryItem *i = dynamic_cast<EntryItem *>( item ) )
		emit entrySelected( entry( i->id() ) );

	item->setOpen( !item->isOpen() );
}
	
QDomElement Glossary::childElement( const QDomElement &element, const QString &name )
{
	QDomElement e;
	for ( e = element.firstChild().toElement(); !e.isNull(); e = e.nextSibling().toElement() )
		if ( e.tagName() == name )
			break;
	return e;
}

QString Glossary::entryToHtml( const GlossaryEntry &entry )
{
    QFile htmlFile( locate("data", "khelpcenter/glossary.html.in" ) );
    if (!htmlFile.open(QIODevice::ReadOnly))
      return QString( "<html><head></head><body><h3>%1</h3>%2</body></html>" )
             .arg( i18n( "Error" ) )
             .arg( i18n( "Unable to show selected glossary entry: unable to open "
                          "file 'glossary.html.in'!" ) );

    QString seeAlso;
    if (!entry.seeAlso().isEmpty()) {
        seeAlso = i18n("See also: ");
        GlossaryEntryXRef::List seeAlsos = entry.seeAlso();
        GlossaryEntryXRef::List::ConstIterator it = seeAlsos.begin();
        GlossaryEntryXRef::List::ConstIterator end = seeAlsos.end();
        for (; it != end; ++it) {
            seeAlso += QLatin1String("<a href=\"glossentry:");
            seeAlso += (*it).id();
            seeAlso += QLatin1String("\">") + (*it).term();
            seeAlso += QLatin1String("</a>, ");
        }
        seeAlso = seeAlso.left(seeAlso.length() - 2);
    }

    QTextStream htmlStream(&htmlFile);
    return htmlStream.read()
           .arg( i18n( "KDE Glossary" ) )
           .arg( entry.term() )
           .arg( View::langLookup( "khelpcenter/konq.css" ) )
           .arg( View::langLookup( "khelpcenter/pointers.png" ) )
           .arg( View::langLookup( "khelpcenter/khelpcenter.png" ) )
           .arg( View::langLookup( "khelpcenter/lines.png" ) )
           .arg( entry.term() )
           .arg( entry.definition() )
           .arg( seeAlso)
           .arg( View::langLookup( "khelpcenter/kdelogo2.png" ) );
}

void Glossary::slotSelectGlossEntry( const QString &id )
{
    EntryItem *newItem = m_idDict.find( id );
    if ( newItem == 0 )
        return;

    EntryItem *curItem = dynamic_cast<EntryItem *>( currentItem() );
    if ( curItem != 0 ) {
        if ( curItem->id() == id )
            return;
        curItem->parent()->setOpen( false );
    }

    setCurrentItem( newItem );
    ensureItemVisible( newItem );
}

#include "glossary.moc"
// vim:ts=4:sw=4:et
