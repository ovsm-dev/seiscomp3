/***************************************************************************
 *   Copyright (C) by ETHZ/SED                                             *
 *                                                                         *
 *   You can redistribute and/or modify this program under the             *
 *   terms of the SeisComP Public License.                                 *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   SeisComP Public License for more details.                             *
 *                                                                         *
 *   Developed by gempa GmbH                                               *
 ***************************************************************************/


#define SEISCOMP_COMPONENT VSConnection

#include <seiscomp3/logging/log.h>
#include <seiscomp3/core/strings.h>
#include <seiscomp3/core/plugin.h>
#include "recordstream.h"


using namespace std;
using namespace Seiscomp;
using namespace Seiscomp::Core;
using namespace Seiscomp::IO;
using namespace Seiscomp::Communication;
using namespace Seiscomp::RecordStream;


REGISTER_RECORDSTREAM(VSConnection, "vs");

ADD_SC_PLUGIN(
	"VS (Virtual Seismologist) record stream interface to acquire envelope values",
	"Jan Becker, gempa GmbH",
	0, 1, 0
)
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
VSConnection::VSConnection()
: RecordStream(), _stream(std::stringstream::in|std::stringstream::binary),
  _queue(NULL) {}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
VSConnection::VSConnection(std::string serverloc)
: RecordStream(), _stream(std::stringstream::in|std::stringstream::binary),
  _queue(NULL) {
    setSource(serverloc);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
VSConnection::~VSConnection() {
	while ( _queue != NULL ) {
		VSRecord *rec = _queue;
		_queue = _queue->next;
		delete rec;
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool VSConnection::setSource(std::string serverloc) {
	close();
	_group = "VS";
	_host = "localhost";

	size_t pos = serverloc.find('/');
	if ( pos != string::npos ) {
		_host = serverloc.substr(0, pos);
		_group = serverloc.substr(pos+1);
		serverloc.erase(pos);
	}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool VSConnection::connect() {
	// Delete all pending records
	while ( _queue != NULL ) {
		VSRecord *rec = _queue;
		_queue = _queue->next;
		delete rec;
	}

	if ( _connection ) {
		SEISCOMP_ERROR("already connected");
		return false;
	}

	int status = 0;
	_connection = Connection::Create(
		_host, "", Protocol::LISTENER_GROUP,
		Protocol::PRIORITY_DEFAULT,
		3000, &status
	);

	if ( !_connection ) {
		SEISCOMP_DEBUG("Could not create connection");
		return false;
	}

	if ( _connection->subscribe(_group) != Status::SEISCOMP_SUCCESS ) {
		close();
		SEISCOMP_DEBUG("Could not subscribe to group %s", _group.c_str());
		return false;
	}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool VSConnection::handle(Seiscomp::DataModel::VS::Envelope *e) {
	VSRecord *last = NULL;

	for ( size_t i = 0; i < e->envelopeChannelCount(); ++i ) {
		Seiscomp::DataModel::VS::EnvelopeChannel *cha = e->envelopeChannel(i);
		cha->name();
		cha->waveformID();
		for ( size_t j = 0; j < cha->envelopeValueCount(); ++j ) {
			Seiscomp::DataModel::VS::EnvelopeValue *val = cha->envelopeValue(j);
			char suffix;

			if ( val->type() == "acc" )
				suffix = 'A';
			else if ( val->type() == "vel" )
				suffix = 'V';
			else if ( val->type() == "disp" )
				suffix = 'D';
			else
				continue;

			string chacode = cha->waveformID().channelCode() + suffix;
			if ( !isRequested(cha->waveformID().networkCode(),
			                  cha->waveformID().stationCode(),
			                  cha->waveformID().locationCode(), chacode) )
				continue;

			VSRecord *rec = new VSRecord;
			float value = (float)val->value();

			rec->setNetworkCode(cha->waveformID().networkCode());
			rec->setStationCode(cha->waveformID().stationCode());
			rec->setLocationCode(cha->waveformID().locationCode());
			rec->setChannelCode(chacode);

			rec->setStartTime(e->timestamp());
			rec->setSamplingFrequency(1.0);
			rec->setDataType(Array::FLOAT);
			rec->setData(1, &value, Array::FLOAT);

			if ( last != NULL ) last->next = rec;
			else _queue = rec;

			last = rec;
		}
	}

	return _queue != NULL;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool VSConnection::setRecordType(const char* type) {
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
Record* VSConnection::createRecord(Array::DataType, Record::Hint) {
	if ( _queue == NULL ) return NULL;
	VSRecord *rec = _queue;
	_queue = _queue->next;
	rec->next = NULL;
	return rec;
}

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool VSConnection::addStream(std::string net, std::string sta, std::string loc, std::string cha) {
	std::pair<Node::List::iterator,bool> itp = _streams.insert(Node(net));

	Node::List &stas = itp.first->childs;
	itp = stas.insert(Node(sta));

	Node::List &locs = itp.first->childs;
	itp = locs.insert(Node(loc));

	Node::List &chas = itp.first->childs;
	itp = chas.insert(Node(cha));

	return itp.second;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool VSConnection::addStream(std::string net, std::string sta, std::string loc, std::string cha,
	const Seiscomp::Core::Time &, const Seiscomp::Core::Time &) {
	return addStream(net, sta, loc, cha);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool VSConnection::removeStream(std::string net, std::string sta, std::string loc, std::string cha) {
	Node::List::iterator nit = _streams.find(Node(net));
	if ( nit == _streams.end() ) return false;

	Node::List &stas = nit->childs;
	Node::List::iterator sit = stas.find(Node(sta));
	if ( sit == stas.end() ) return false;

	Node::List &locs = sit->childs;
	Node::List::iterator lit = locs.find(Node(loc));
	if ( lit == locs.end() ) return false;

	Node::List &chas = lit->childs;
	Node::List::iterator cit = chas.find(Node(cha));
	if ( cit == chas.end() ) return false;

	chas.erase(cit);

	if ( !chas.empty() ) return true;

	locs.erase(lit);
	if ( !locs.empty() ) return true;

	stas.erase(sit);
	if ( !stas.empty() ) return true;

	_streams.erase(nit);
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool VSConnection::setStartTime(const Seiscomp::Core::Time &) {
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool VSConnection::setEndTime(const Seiscomp::Core::Time &) {
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool VSConnection::setTimeWindow(const Seiscomp::Core::TimeWindow &w) {
	return setStartTime(w.startTime()) && setEndTime(w.endTime());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool VSConnection::setTimeout(int seconds) {
	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool VSConnection::clear() {
	if ( _connection ) {
		_connection->disconnect();
		_connection = NULL;
	}
	_streams.clear();
	_stream.clear();
	_stream.str(string());
	_closeRequested = false;

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool VSConnection::isRequested(const std::string &net, const std::string &sta,
                               const std::string &loc, const std::string &cha) const {
	Node::List::iterator it = Node::findMatch(_streams, net);
	if ( it == _streams.end() ) return false;

	const Node::List &stas = it->childs;
	it = Node::findMatch(stas, sta);
	if ( it == stas.end() ) return false;

	const Node::List &locs = it->childs;
	it = Node::findMatch(locs, loc);
	if ( it == locs.end() ) return false;

	const Node::List &chas = it->childs;
	it = Node::findMatch(chas, cha);
	if ( it == chas.end() ) return false;

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
// Hopefully safe to be called from another thread
void VSConnection::close() {
	_closeRequested = true;
	if ( _connection ) _connection->disconnect();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
istream &VSConnection::stream() {
	if ( _queue != NULL ) return _stream;

	if ( !_connection ) {
		if ( !connect() ) {
			SEISCOMP_ERROR("Connection failed");
			_stream.clear(ios::eofbit);
			return _stream;
		}
		_closeRequested = false;
	}

	while ( !_closeRequested ) {
		int error;

		Message *msg = _connection->readMessage(true, Connection::READ_ALL, NULL, &error);
		if ( msg == NULL ) continue;
		if ( error == Core::Status::SEISCOMP_SUCCESS ) {
			for ( MessageIterator it = msg->iter(); *it; ++it ) {
				DataModel::VS::Envelope* e = DataModel::VS::Envelope::Cast(*it);
				if ( e != NULL ) {
					if ( handle(e) ) return _stream;
				}
			}
		}
		else if ( !_closeRequested ) {
			if ( msg ) delete msg;

			if ( _connection->isConnected() ) continue;

			SEISCOMP_WARNING("Connection lost, trying to reconnect");
			bool first = true;
			while ( !_closeRequested ) {
				_connection->reconnect();
				if ( _connection->isConnected() ) {
					SEISCOMP_INFO("Reconnected successfully");
					continue;
				}
				else {
					if ( first ) {
						first = false;
						SEISCOMP_INFO("Reconnecting failed, trying again every 2 seconds");
					}
					sleep(2);
				}
			}

			if ( _closeRequested )
				break;
		}
		else {
			_stream.clear(ios::eofbit);
			break;
		}
	}

	return _stream;
}
