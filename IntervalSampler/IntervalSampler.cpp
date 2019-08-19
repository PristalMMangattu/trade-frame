/************************************************************************
 * Copyright(c) 2019, One Unified. All rights reserved.                 *
 * email: info@oneunified.net                                           *
 *                                                                      *
 * This file is provided as is WITHOUT ANY WARRANTY                     *
 *  without even the implied warranty of                                *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                *
 *                                                                      *
 * This software may not be used nor distributed without proper license *
 * agreement.                                                           *
 *                                                                      *
 * See the file LICENSE.txt for redistribution information.             *
 ************************************************************************/

/*
 * File:    main.cpp
 * Author:  raymond@burkholder.net
 * Project: IntervalSampler
 * Created on August 6, 2019, 11:08 AM
 */

// An IQFeed based project to collect data from symbols at an interval

// assumption:  all symbols are 'stocks'.
//   lookups will be required if non-stock symbols are provided (or not, if no special processing is required)


#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

#include <boost/filesystem.hpp>

#include <boost/lexical_cast.hpp>

#include <OUCommon/TimeSource.h>

#include "ReadSymbolFile.h"
#include "IntervalSampler.h"

IMPLEMENT_APP(AppIntervalSampler)

bool AppIntervalSampler::OnInit() {

  wxApp::OnInit();
  wxApp::SetAppDisplayName( "Interval Sampler" );
  wxApp::SetVendorName( "One Unified Net Limited" );
  wxApp::SetVendorDisplayName( "(c) 2019 One Unified Net Limited" );

  m_sStateFileName = "IntervalSampler.state";

  m_pFrameMain = new FrameMain( 0, wxID_ANY, "Interval Sampler" );
  wxWindowID idFrameMain = m_pFrameMain->GetId();
  //m_pFrameMain->Bind( wxEVT_SIZE, &AppStrategy1::HandleFrameMainSize, this, idFrameMain );
  //m_pFrameMain->Bind(
  //  wxEVT_SIZE,
  //  [this](wxSizeEvent& event){
  //    std::cout << "w=" << event.GetSize().GetWidth() << ",h=" << event.GetSize().GetHeight() << std::endl;
  //    event.Skip();
  //    },
  //  idFrameMain );
  //m_pFrameMain->Bind( wxEVT_MOVE, &AppStrategy1::HandleFrameMainMove, this, idFrameMain );
  //m_pFrameMain->Center();
  //m_pFrameMain->Move( 200, 100 );
  //m_pFrameMain->SetSize( 1400, 800 );
  SetTopWindow( m_pFrameMain );

  wxBoxSizer* sizerMain = new wxBoxSizer( wxVERTICAL );
  m_pFrameMain->SetSizer( sizerMain );

  m_pPanelLogging = new ou::tf::PanelLogging( m_pFrameMain, wxID_ANY );
  sizerMain->Add( m_pPanelLogging, 1, wxALL | wxEXPAND|wxALIGN_LEFT|wxALIGN_RIGHT|wxALIGN_TOP|wxALIGN_BOTTOM, 0);
  m_pPanelLogging->Show( true );

  m_pFrameMain->Bind( wxEVT_CLOSE_WINDOW, &AppIntervalSampler::OnClose, this );  // start close of windows and controls
  m_pFrameMain->Show( true );

  static const std::string sFileName( "../is_symbols.txt" );

  bool bOk( true );

  if ( !boost::filesystem::exists( sFileName ) ) {
    std::cout << "file " << sFileName << " cannot be found" << std::endl;
    bOk = false;
  }
  else {
    try {
      ReadSymbolFile symbols( sFileName, m_vSymbol );
    }
    catch( std::exception& e ) {
      std::cout << "error during parsing symbols.txt: " << e.what() << std::endl;
      //wxApp::Exit();
      bOk = false;
    }

    if ( bOk ) {
      m_pIQFeed = boost::make_shared<ou::tf::IQFeedProvider>();
      m_bIQFeedConnected = false;

      m_pIQFeed->OnConnecting.Add( MakeDelegate( this, &AppIntervalSampler::HandleIQFeedConnecting ) );
      m_pIQFeed->OnConnected.Add( MakeDelegate( this, &AppIntervalSampler::HandleIQFeedConnected ) );
      m_pIQFeed->OnDisconnecting.Add( MakeDelegate( this, &AppIntervalSampler::HandleIQFeedDisconnecting ) );
      m_pIQFeed->OnDisconnected.Add( MakeDelegate( this, &AppIntervalSampler::HandleIQFeedDisconnected ) );
      m_pIQFeed->OnError.Add( MakeDelegate( this, &AppIntervalSampler::HandleIQFeedError ) );

      m_pIQFeed->Connect();
    }
  }

  CallAfter(
    [this](){
      LoadState();
    }
  );

  return true;
}

void AppIntervalSampler::HandleIQFeedConnecting( int e ) {  // cross thread event
  std::cout << "IQFeed connecting ..." << std::endl;
}

void AppIntervalSampler::OutputFileOpen( ptime dt ) {
  std::string sdt = boost::posix_time::to_iso_string( dt );
  std::string sFileName = sdt + ".csv";
  m_out.open( sFileName, std::fstream::out );
  if ( !m_out.is_open() ) {
    std::cerr << "cannot open file for output " << sFileName << std::endl;
    std::runtime_error( "file not opened" );
  }
  std::cout << "streaming to " << sFileName << std::endl;
}

void AppIntervalSampler::OutputFileCheck( ptime dt ) {

  namespace pt = boost::posix_time;

  //ptime dt( ou::TimeSource::Instance().External() );
  if ( !m_out.is_open() ) {
    OutputFileOpen( dt );
    m_dtNextRotation = pt::ptime( dt.date(), pt::time_duration( 21, 30, 0 ) ); // UTC for eastern time
    if ( m_dtNextRotation <= dt ) {
      m_dtNextRotation = m_dtNextRotation + boost::gregorian::date_duration( 1 );
    }
    std::cout << "  next rotation at " << boost::posix_time::to_iso_string( m_dtNextRotation ) << std::endl;
    //m_dtNextRotation = pt::ptime( dt.date() + boost::gregorian::date_duration( 1 ), pt::time_duration( 21, 30, 0 ) ); // UTC for eastern time
    std::cout << "  note: trade values are open of next bar" << std::endl;
  }
  else {
    if ( m_dtNextRotation <= dt ) {
      m_out.close();
      OutputFileCheck( dt ); // recursive one step max
    }
  }
}

void AppIntervalSampler::HandleIQFeedConnected( int e ) {  // cross thread event

  m_bIQFeedConnected = true;
  std::cout << "IQFeed connected." << std::endl;

  assert( 1 < m_vSymbol.size() );
  size_t nSeconds = boost::lexical_cast<size_t>( m_vSymbol[ 0 ] );
  std::cout << "interval: " << m_vSymbol[ 0 ] << " seconds" << std::endl;

  vSymbol_t::const_iterator iterSymbol = m_vSymbol.begin();
  iterSymbol++;
  m_vWatch.resize( m_vSymbol.size() - 1 );
  vWatch_t::iterator iterWatch = m_vWatch.begin();
  while ( m_vSymbol.end() != iterSymbol ) {
    ou::tf::Instrument::pInstrument_t pInstrument
      = boost::make_shared<ou::tf::Instrument>( *iterSymbol, ou::tf::InstrumentType::Stock, "SMART" );
    pWatch_t pWatch = boost::make_shared<ou::tf::Watch>( pInstrument, m_pIQFeed );
    iterWatch->Assign( nSeconds, pWatch,
                       [this](const ou::tf::Instrument::idInstrument_t& idInstrument,
                            size_t nSequence,
                            const ou::tf::Bar& bar,
                            const ou::tf::Quote& quote,
                            const ou::tf::Trade& trade){
                         OutputFileCheck( trade.DateTime() );
                         m_out
                           << idInstrument
                           << "," << nSequence
                           << "," << boost::posix_time::to_iso_string( trade.DateTime() )
                           << "," << bar.High()
                           << "," << bar.Low()
                           << "," << bar.Open()
                           << "," << bar.Close()
                           << "," << bar.Volume()
                           << "," << trade.Price() << "," << trade.Volume()
                           << "," << quote.Ask() << "," << quote.AskSize()
                           << "," << quote.Bid() << "," << quote.BidSize()
                           << std::endl;
                       } );
    iterSymbol++;
    iterWatch++;
  }

}

void AppIntervalSampler::HandleIQFeedDisconnecting( int e ) {  // cross thread event
  std::cout << "IQFeed disconnecting ..." << std::endl;
}

void AppIntervalSampler::HandleIQFeedDisconnected( int e ) { // cross thread event
  if ( m_out.is_open() ) {
    m_out.close();
  }
  m_bIQFeedConnected = false;
  std::cout << "IQFeed disconnected." << std::endl;
}

void AppIntervalSampler::HandleIQFeedError( size_t e ) {
  std::cout << "HandleIQFeedError: " << e << std::endl;
}

void AppIntervalSampler::SaveState() {
  std::cout << "Saving Config ..." << std::endl;
  std::ofstream ofs( m_sStateFileName );
  boost::archive::text_oarchive oa(ofs);
  oa & *this;
  std::cout << "  done." << std::endl;
}

void AppIntervalSampler::LoadState() {
  try {
    std::cout << "Loading Config ..." << std::endl;
    std::ifstream ifs( m_sStateFileName );
    boost::archive::text_iarchive ia(ifs);
    ia & *this;
    std::cout << "  done." << std::endl;
  }
  catch(...) {
    std::cout << "load exception" << std::endl;
  }
}

void AppIntervalSampler::OnClose( wxCloseEvent& event ) { // step 1

  SaveState();

  event.Skip();  // auto followed by Destroy();
}

int AppIntervalSampler::OnExit() { // step 2

  m_vWatch.clear();

  if ( m_pIQFeed ) {
    if ( m_pIQFeed->Connected() ) {
      m_pIQFeed->Disconnect();

      m_pIQFeed->OnConnecting.Remove( MakeDelegate( this, &AppIntervalSampler::HandleIQFeedConnecting ) );
      m_pIQFeed->OnConnected.Remove( MakeDelegate( this, &AppIntervalSampler::HandleIQFeedConnected ) );
      m_pIQFeed->OnDisconnecting.Remove( MakeDelegate( this, &AppIntervalSampler::HandleIQFeedDisconnecting ) );
      m_pIQFeed->OnDisconnected.Remove( MakeDelegate( this, &AppIntervalSampler::HandleIQFeedDisconnected ) );
      m_pIQFeed->OnError.Remove( MakeDelegate( this, &AppIntervalSampler::HandleIQFeedError ) );
    }

    if ( m_out.is_open() ) { // TODO: duplicate of 'disconnected, but disconnected not reached, need a join'
      m_out.close();
    }
  }

  return wxApp::OnExit();
}

