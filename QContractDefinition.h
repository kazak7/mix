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
/** @file QContractDefinition.h
 * @author Yann yann@ethdev.com
 * @date 2014
 */

#pragma once

#include <QObject>
#include "libsolidity/AST.h"
#include "QFunctionDefinition.h"
#include "QBasicNodeDefinition.h"

namespace dev
{
namespace mix
{

class QContractDefinition: public QBasicNodeDefinition
{
	Q_OBJECT
	Q_PROPERTY(QList<QObject*> functions READ functions)

public:
	QContractDefinition(std::shared_ptr<dev::solidity::ContractDefinition> _contract);
	QList<QObject*> functions() { return m_functions; }
	static std::shared_ptr<QContractDefinition> Contract(QString _code);

private:
	QList<QObject*> m_functions;
	void initQFunctions();
};

}
}
