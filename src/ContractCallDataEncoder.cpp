/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file ContractCallDataEncoder.cpp
 * @author Yann yann@ethdev.com
 * @date 2014
 * Ethereum IDE client.
 */

#include <vector>
#include <QtCore/qmath.h>
#include <QMap>
#include <QStringList>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <libethcore/CommonJS.h>
#include <libsolidity/AST.h>
#include "QVariableDeclaration.h"
#include "QVariableDefinition.h"
#include "QFunctionDefinition.h"
#include "ContractCallDataEncoder.h"
using namespace std;
using namespace dev;
using namespace dev::solidity;
using namespace dev::mix;

bytes ContractCallDataEncoder::encodedData()
{
	bytes r(m_encodedData);
	size_t headerSize = m_encodedData.size() & ~0x1fUL; //ignore any prefix that is not 32-byte aligned
	//apply offsets
	for (auto const& p: m_dynamicOffsetMap)
	{
		vector_ref<byte> offsetRef(m_dynamicData.data() + p.first, 32);
		toBigEndian(p.second + headerSize, offsetRef); //add header size minus signature hash
	}
	for (auto const& p: m_staticOffsetMap)
	{
		vector_ref<byte> offsetRef(r.data() + p.first, 32);
		toBigEndian(p.second + headerSize, offsetRef); //add header size minus signature hash
	}
	if (m_dynamicData.size() > 0)
		r.insert(r.end(), m_dynamicData.begin(), m_dynamicData.end());
	return r;
}

void ContractCallDataEncoder::encode(QFunctionDefinition const* _function)
{
	bytes hash = _function->hash().asBytes();
	m_encodedData.insert(m_encodedData.end(), hash.begin(), hash.end());
}

void ContractCallDataEncoder::encodeArray(QJsonArray const& _array, SolidityType const& _type, bytes& _content)
{
	size_t offsetStart = _content.size();
	if (_type.dynamicSize)
	{
		bytes count = bytes(32);
		toBigEndian((u256)_array.size(), count);
		_content += count; //reserved space for count
	}

	int k = 0;
	for (QJsonValue const& c: _array)
	{
		if (c.isArray())
		{
			if (_type.baseType->dynamicSize)
				m_dynamicOffsetMap.push_back(std::make_pair(m_dynamicData.size() + offsetStart + 32 + k * 32, m_dynamicData.size() + _content.size()));
			encodeArray(c.toArray(), *_type.baseType, _content);
		}
		else
		{
			// encode single item
			if (c.isDouble())
				encodeSingleItem(QString::number(c.toDouble()), _type, _content);
			else if (c.isString())
				encodeSingleItem(c.toString(), _type, _content);
		}
		k++;
	}
}

void ContractCallDataEncoder::encode(QVariant const& _data, SolidityType const& _type)
{
	if (_type.dynamicSize && (_type.type == SolidityType::Type::Bytes || _type.type == SolidityType::Type::String))
	{
		bytes empty(32);
		size_t sizePos = m_dynamicData.size();
		m_dynamicData += empty; //reserve space for count
		encodeSingleItem(_data.toString(), _type, m_dynamicData);
		vector_ref<byte> sizeRef(m_dynamicData.data() + sizePos, 32);
		toBigEndian(static_cast<unsigned>(_data.toString().size()), sizeRef);
		m_staticOffsetMap.push_back(std::make_pair(m_encodedData.size(), sizePos));
		m_encodedData += empty; //reserve space for offset
	}
	else if (_type.array)
	{
		bytes content;
		size_t size = m_encodedData.size();
		if (_type.dynamicSize)
		{
			m_encodedData += bytes(32); // reserve space for offset
			m_staticOffsetMap.push_back(std::make_pair(size, m_dynamicData.size()));
		}
		QJsonDocument jsonDoc = QJsonDocument::fromJson(_data.toString().toUtf8());
		encodeArray(jsonDoc.array(), _type, content);

		if (!_type.dynamicSize)
			m_encodedData.insert(m_encodedData.end(), content.begin(), content.end());
		else
			m_dynamicData.insert(m_dynamicData.end(), content.begin(), content.end());
	}
	else
		encodeSingleItem(_data.toString(), _type, m_encodedData);
}

unsigned ContractCallDataEncoder::encodeSingleItem(QString const& _data, SolidityType const& _type, bytes& _dest)
{
	if (_type.type == SolidityType::Type::Struct)
		BOOST_THROW_EXCEPTION(dev::Exception() << dev::errinfo_comment("Struct parameters are not supported yet"));

	unsigned const alignSize = 32;
	QString src = _data;
	bytes result;

	if ((src.startsWith("\"") && src.endsWith("\"")) || (src.startsWith("\'") && src.endsWith("\'")))
		src = src.remove(src.length() - 1, 1).remove(0, 1);

	if (src.startsWith("0x"))
	{
		result = fromHex(src.toStdString().substr(2));
		if (_type.type != SolidityType::Type::Bytes)
			result = padded(result, alignSize);
	}
	else
	{
		try
		{
			bigint i(src.toStdString());
			result = bytes(alignSize);
			toBigEndian((u256)i, result);
		}
		catch (std::exception const&)
		{
			// manage input as a string.
			result = encodeStringParam(src, alignSize);
		}
	}

	size_t dataSize = _type.dynamicSize ? result.size() : alignSize;
	if (result.size() % alignSize != 0)
		result.resize((result.size() & ~(alignSize - 1)) + alignSize);
	_dest.insert(_dest.end(), result.begin(), result.end());
	return dataSize;
}

bigint ContractCallDataEncoder::decodeInt(dev::bytes const& _rawValue)
{
	dev::u256 un = dev::fromBigEndian<dev::u256>(_rawValue);
	if (un >> 255)
		return (-s256(~un + 1));
	return un;
}

QString ContractCallDataEncoder::toString(dev::bigint const& _int)
{
	std::stringstream str;
	str << std::dec << _int;
	return QString::fromStdString(str.str());
}

dev::bytes ContractCallDataEncoder::encodeBool(QString const& _str)
{
	bytes b(1);
	b[0] = _str == "1" || _str.toLower() == "true " ? 1 : 0;
	return padded(b, 32);
}

bool ContractCallDataEncoder::decodeBool(dev::bytes const& _rawValue)
{
	byte ret = _rawValue.at(_rawValue.size() - 1);
	return (ret != 0);
}

QString ContractCallDataEncoder::toString(bool _b)
{
	return _b ? "true" : "false";
}

dev::bytes ContractCallDataEncoder::encodeStringParam(QString const& _str, unsigned alignSize)
{
	bytes result;
	QByteArray bytesAr = _str.toLocal8Bit();
	result = bytes(bytesAr.begin(), bytesAr.end());
	return paddedRight(result, alignSize);
}

dev::bytes ContractCallDataEncoder::encodeBytes(QString const& _str)
{
	QByteArray bytesAr = _str.toLocal8Bit();
	bytes r = bytes(bytesAr.begin(), bytesAr.end());
	return padded(r, 32);
}

dev::bytes ContractCallDataEncoder::decodeBytes(dev::bytes const& _rawValue)
{
	return _rawValue;
}

QVariantList ContractCallDataEncoder::decodeStruct(SolidityType const& _type, dev::bytes const& _rawValue, int& _offset)
{
	QVariantList list;
	int pos = static_cast<int>(_offset);
	for (auto const& m: _type.members)
		list.push_back(decodeType(m.type, _rawValue, pos));
	return list;
}

QString ContractCallDataEncoder::toString(dev::bytes const& _b)
{
	return QString::fromStdString(dev::toJS(_b));
}

QString ContractCallDataEncoder::toChar(dev::bytes const& _b)
{
	QString str;
	asString(_b, str);
	return  str;
}

QJsonValue ContractCallDataEncoder::decodeArrayContent(SolidityType const& _type, bytes const& _value, int& pos)
{
	if (_type.baseType->array)
	{
		QJsonArray sub = decodeArray(*_type.baseType, _value, pos);
		return sub;
	}
	else
	{
		bytesConstRef value(_value.data() + pos, 32);
		bytes rawParam(32);
		value.populate(&rawParam);
		QVariant i = decode(*_type.baseType, rawParam);
		pos = pos + 32;
		return i.toString();
	}
}

QJsonArray ContractCallDataEncoder::decodeArray(SolidityType const& _type, bytes const& _value, int& pos)
{
	QJsonArray array;
	if (_value.size() < static_cast<unsigned>(pos))
		return array;
	bytesConstRef value(&_value);
	int count = 0;
	bigint offset = pos;
	int valuePosition = pos;
	if (!_type.dynamicSize)
		count = _type.count;
	else
	{
		bytesConstRef value(_value.data() + pos, 32); // offset
		bytes rawParam(32);
		value.populate(&rawParam);
		offset = decodeInt(rawParam);
		valuePosition = static_cast<int>(offset) + 32;
		pos += 32;
		value = bytesConstRef(_value.data() + static_cast<int>(offset), 32); // count
		value.populate(&rawParam);
		count = static_cast<int>(decodeInt(rawParam));
	}
	if (_type.type == QSolidityType::Type::Bytes || _type.type == QSolidityType::Type::String)
	{
		bytesConstRef value(_value.data() + (static_cast<int>(offset) + 32), 32);
		bytes rawParam(count);
		value.populate(&rawParam);
		if (_type.type == QSolidityType::Type::Bytes)
			array.append(toString(decodeBytes(rawParam)));
		else
			array.append(toChar(decodeBytes(rawParam)));
	}
	else
	{
		for (int k = 0; k < count; ++k)
		{
			if (_type.dynamicSize)
				array.append(decodeArrayContent(_type, _value, valuePosition));
			else
				array.append(decodeArrayContent(_type, _value, pos));
		}
	}
	return array;
}

QVariant ContractCallDataEncoder::decode(SolidityType const& _type, bytes const& _value, unsigned const& offset)
{
	if (_value.size() < offset)
		return QVariant();
	bytes rawParam(32);
	if (offset)
	{
		bytesConstRef value(_value.data() + static_cast<int>(offset), 32);
		value.populate(&rawParam);
	}
	else
	{
		bytesConstRef value(&_value);
		value.populate(&rawParam);
	}

	QSolidityType::Type type = _type.type;
	if (type == QSolidityType::Type::SignedInteger || type == QSolidityType::Type::UnsignedInteger)
		return QVariant::fromValue(toString(decodeInt(rawParam)));
	else if (type == QSolidityType::Type::Bool)
		return QVariant::fromValue(toString(decodeBool(rawParam)));
	else if (type == QSolidityType::Type::Bytes || type == QSolidityType::Type::Hash)
		return QVariant::fromValue(toString(decodeBytes(rawParam)));
	else if (type == QSolidityType::Type::String)
		return QVariant::fromValue(toChar(decodeBytes(rawParam)));
	else if (type == QSolidityType::Type::Struct)
	{
		int pos = offset;
		return decodeStruct(_type, _value, pos);
	}
	else if (type == QSolidityType::Type::Address)
		return QVariant::fromValue(toString(decodeBytes(unpadLeft(rawParam))));
	else if (type == QSolidityType::Type::Enum)
		return QVariant::fromValue(decodeEnum(rawParam));
	else
		BOOST_THROW_EXCEPTION(Exception() << errinfo_comment("Parameter declaration not found"));
}

QVariant ContractCallDataEncoder::decodeRawArray(SolidityType const& _type, bytes const& _value, int& pos)
{
	if (_value.size() < (size_t)pos)
		return QVariant();
	int count = _type.count;
	if (_type.dynamicSize)
	{
		bytesConstRef value(_value.data() + pos, 32); // count
		bytes rawParam(32);
		value.populate(&rawParam);
		count =  static_cast<int>(decodeInt(rawParam));
		pos = pos + 32; //cursor to content
	}

	QJsonArray array;
	for (int k = 0; k < count; ++k)
		array.append(decodeArrayContent(_type, _value, pos));
	QJsonDocument jsonDoc = QJsonDocument::fromVariant(array.toVariantList());
	return jsonDoc.toJson(QJsonDocument::Compact);
}

QVariant ContractCallDataEncoder::formatStorageValue(SolidityType const& _type, unordered_map<u256, u256> const& _storage, unsigned _offset, u256 const& _slot)
{
	dev::bytes b;
	for (auto const& sto: _storage)
	{
		u256 slotValue = sto.second;
		bytes slotBytes = toBigEndian(slotValue);
		b += slotBytes;
	}
	int pos = _offset + static_cast<int>(_slot) * 32;
	if (b.size() > static_cast<unsigned>(pos))
	{
		QVariantList items;
		ContractCallDataEncoder decoder;
		if (_type.array)
			return decodeRawArray(_type, b, pos);
		else if (_type.type == SolidityType::Struct)
		{
			for (auto const& m: _type.members)
			{
				if (m.type.array)
					items.append(decodeRawArray(m.type, b, pos));
				else
					items.append(decoder.decodeType(m.type, b, pos));
			}
		}
		return items;
	}
	else
		return QVariant();
}


QString ContractCallDataEncoder::decodeEnum(bytes _value)
{
	return toString(decodeInt(_value));
}

QString ContractCallDataEncoder::decode(QVariableDeclaration* const& _param, bytes _value, unsigned const& _offset)
{
	SolidityType const& type = _param->type()->type();
	return decode(type, _value, _offset).toString();
}

QStringList ContractCallDataEncoder::decode(QList<QVariableDeclaration*> const& _returnParameters, bytes _value, unsigned const& _offset)
{
	bytes _v = _value;
	QStringList r;
	int readPosition = 0;
	if (_offset)
		readPosition = _offset;
	for (int k = 0; k <_returnParameters.length(); k++)
	{
		QVariableDeclaration* dec = static_cast<QVariableDeclaration*>(_returnParameters.at(k));
		SolidityType const& type = dec->type()->type();
		QString ret = decodeType(type, _value, readPosition).toString();
		r.append(ret);
	}
	return r;
}

QVariant ContractCallDataEncoder::decodeType(SolidityType _type, bytes _value, int& _readPosition)
{
	if (_value.size() < static_cast<unsigned>(_readPosition))
		return QVariant();
	if (_type.array)
	{
		QJsonArray array = decodeArray(_type, _value, _readPosition);
		if (_type.type == SolidityType::String && array.count() <= 1)
		{
			if (array.count() == 1)
				return array[0].toString();
			else
				return QString();
		}
		else
		{
			QJsonDocument jsonDoc = QJsonDocument::fromVariant(array.toVariantList());
			return jsonDoc.toJson(QJsonDocument::Compact);
		}
	}
	else if (_type.members.size() > 0)
	{
		QVariantList ret = decodeStruct(_type, _value, _readPosition);
		return ret;
	}
	else
	{
		bytesConstRef value(_value.data() + _readPosition, 32);
		bytes rawParam(32);
		value.populate(&rawParam);
		_readPosition += 32;
		return decode(_type, rawParam).toString();
	}
}


bool ContractCallDataEncoder::asString(dev::bytes const& _b, QString& _str)
{
	dev::bytes bunPad = unpadded(_b);
	for (unsigned i = 0; i < bunPad.size(); i++)
	{
		if (bunPad.at(i) < 9 || bunPad.at(i) > 127)
			return false;
		else
			_str += QString::fromStdString(dev::toJS(bunPad.at(i))).replace("0x", "");
	}
	return true;
}
