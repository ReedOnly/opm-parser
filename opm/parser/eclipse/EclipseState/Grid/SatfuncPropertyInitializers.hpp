/*
  Copyright 2014 Andreas Lauser

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef ECLIPSE_SATFUNCPROPERTY_INITIALIZERS_HPP
#define ECLIPSE_SATFUNCPROPERTY_INITIALIZERS_HPP

#include <vector>
#include <string>
#include <exception>
#include <memory>
#include <limits>
#include <algorithm>
#include <cassert>

#include <opm/parser/eclipse/EclipseState/Tables/Tabdims.hpp>
#include <opm/parser/eclipse/EclipseState/Tables/TableContainer.hpp>
#include <opm/parser/eclipse/EclipseState/Tables/TableManager.hpp>

#include <opm/parser/eclipse/EclipseState/Tables/SwofTable.hpp>
#include <opm/parser/eclipse/EclipseState/Tables/SgofTable.hpp>
#include <opm/parser/eclipse/EclipseState/Tables/SwfnTable.hpp>
#include <opm/parser/eclipse/EclipseState/Tables/SgfnTable.hpp>
#include <opm/parser/eclipse/EclipseState/Tables/Sof3Table.hpp>
#include <opm/parser/eclipse/EclipseState/Tables/SlgofTable.hpp>

namespace Opm {

// forward definitions
class Deck;
class EclipseState;
class EnptvdTable;
class ImptvdTable;


class EndpointInitializer
    : public GridPropertyBaseInitializer<double>
{
public:
    enum SaturationFunctionFamily { noFamily = 0, FamilyI = 1, FamilyII = 2};

    /*
      See the "Saturation Functions" chapter in the Eclipse Technical
      Description; there are several alternative families of keywords
      which can be used to enter relperm and capillary pressure
      tables.
    */


protected:

    /*
      The method here goes through the saturation function tables
      Either family I (SWOF,SGOF) or family II (SWFN, SGFN and SOF3)
      must be specified. Other keyword alternatives like SOF2
      and SGWFN and the two dimensional saturation tables
      are currently not supported.

      ** Must be fixed. **
      */


    void findSaturationEndpoints( const EclipseState& m_eclipseState ) const {
        auto tables = m_eclipseState.getTableManager();
        auto tabdims = tables->getTabdims();
        size_t numSatTables = tabdims->getNumSatTables();
        m_minWaterSat.resize( numSatTables , 0 );
        m_maxWaterSat.resize( numSatTables , 0 );
        m_minGasSat.resize( numSatTables , 0 );
        m_maxGasSat.resize( numSatTables , 0 );

        switch (getSaturationFunctionFamily( m_eclipseState )) {
        case SaturationFunctionFamily::FamilyI:
        {
            const TableContainer& swofTables = tables->getSwofTables();
            for (size_t tableIdx = 0; tableIdx < numSatTables; ++tableIdx) {
                const SwofTable& swofTable = swofTables.getTable<SwofTable>(tableIdx);
                m_minWaterSat[tableIdx] = swofTable.getSwColumn().front();
                m_maxWaterSat[tableIdx] = swofTable.getSwColumn().back();
            }

            {
                const TableContainer& sgofTables  = tables->getSgofTables();
                const TableContainer& slgofTables = tables->getSlgofTables();

                if (!sgofTables.empty()) {
                    for (size_t tableIdx = 0; tableIdx < numSatTables; ++tableIdx) {
                        const SgofTable& sgofTable = sgofTables.getTable<SgofTable>( tableIdx );
                        m_minGasSat[tableIdx] = sgofTable.getSgColumn().front();
                        m_maxGasSat[tableIdx] = sgofTable.getSgColumn().back();
                    }
                }
                else {
                    assert(!slgofTables.empty());
                    for (size_t tableIdx = 0; tableIdx < numSatTables; ++tableIdx) {
                        const SlgofTable& slgofTable = slgofTables.getTable<SlgofTable>( tableIdx );
                        m_minGasSat[tableIdx] = 1.0 - slgofTable.getSlColumn().back();
                        m_maxGasSat[tableIdx] = 1.0 - slgofTable.getSlColumn().front();
                    }
                }
            }

            break;
        }
        case SaturationFunctionFamily::FamilyII:
        {
            const TableContainer& swfnTables = tables->getSwfnTables();
            const TableContainer& sgfnTables = tables->getSgfnTables();
            for (size_t tableIdx = 0; tableIdx < numSatTables; ++tableIdx) {
                const SwfnTable& swfnTable = swfnTables.getTable<SwfnTable>(tableIdx);
                const SgfnTable& sgfnTable = sgfnTables.getTable<SgfnTable>(tableIdx);

                m_minWaterSat[tableIdx] = swfnTable.getSwColumn().front();
                m_maxWaterSat[tableIdx] = swfnTable.getSwColumn().back();

                m_minGasSat[tableIdx] = sgfnTable.getSgColumn().front();
                m_maxGasSat[tableIdx] = sgfnTable.getSgColumn().back();
            }
            break;
        }
        default:
            throw std::domain_error("No valid saturation keyword family specified");
        }
    }


    void findCriticalPoints( const EclipseState& m_eclipseState ) const {
        auto tables = m_eclipseState.getTableManager();
        auto tabdims = tables->getTabdims();
        size_t numSatTables = tabdims->getNumSatTables();

        m_criticalWaterSat.resize( numSatTables , 0 );
        m_criticalGasSat.resize( numSatTables , 0 );
        m_criticalOilOGSat.resize( numSatTables , 0 );
        m_criticalOilOWSat.resize( numSatTables , 0 );

        switch (getSaturationFunctionFamily( m_eclipseState )) {
        case SaturationFunctionFamily::FamilyI:
        {
            const TableContainer& swofTables = tables->getSwofTables();

            for (size_t tableIdx = 0; tableIdx < numSatTables; ++tableIdx) {
                // find the critical water saturation
                const SwofTable& swofTable = swofTables.getTable<SwofTable>( tableIdx );
                int numRows = swofTable.numRows();
                const auto &krwCol = swofTable.getKrwColumn();
                for (int rowIdx = 0; rowIdx < numRows; ++rowIdx) {
                    if (krwCol[rowIdx] > 0.0) {
                        double Sw = 0.0;
                        if (rowIdx > 0)
                            Sw = swofTable.getSwColumn()[rowIdx - 1];
                        m_criticalWaterSat[tableIdx] = Sw;
                        break;
                    }
                }

                // find the critical oil saturation of the water-oil system
                const auto &kroOWCol = swofTable.getKrowColumn();
                for (int rowIdx = numRows - 1; rowIdx >= 0; --rowIdx) {
                    if (kroOWCol[rowIdx] > 0.0) {
                        double Sw = swofTable.getSwColumn()[rowIdx + 1];
                        m_criticalOilOWSat[tableIdx] = 1 - Sw;
                        break;
                    }
                }
            }

            {
                const TableContainer& sgofTables = tables->getSgofTables();
                const TableContainer& slgofTables = tables->getSlgofTables();

                if (!sgofTables.empty()) {
                    for (size_t tableIdx = 0; tableIdx < numSatTables; ++tableIdx) {
                        const SgofTable& sgofTable = sgofTables.getTable<SgofTable>( tableIdx );
                        // find the critical gas saturation
                        int numRows = sgofTable.numRows();
                        const auto &krgCol = sgofTable.getKrgColumn();
                        for (int rowIdx = 0; rowIdx < numRows; ++rowIdx) {
                            if (krgCol[rowIdx] > 0.0) {
                                double Sg = 0.0;
                                if (rowIdx > 0)
                                    Sg = sgofTable.getSgColumn()[rowIdx - 1];
                                m_criticalGasSat[tableIdx] = Sg;
                                break;
                            }
                        }

                        // find the critical oil saturation of the oil-gas system
                        const auto &kroOGCol = sgofTable.getKrogColumn();
                        for (int rowIdx = numRows - 1; rowIdx >= 0; --rowIdx) {
                            if (kroOGCol[rowIdx] > 0.0) {
                                double Sg = sgofTable.getSgColumn()[rowIdx + 1];
                                m_criticalOilOGSat[tableIdx] = 1 - Sg;
                                break;
                            }
                        }
                    }
                }
                else {
                    assert(!slgofTables.empty());
                    for (size_t tableIdx = 0; tableIdx < numSatTables; ++tableIdx) {
                        const SlgofTable& slgofTable = slgofTables.getTable<SlgofTable>( tableIdx );
                        // find the critical gas saturation
                        int numRows = slgofTable.numRows();
                        const auto &krgCol = slgofTable.getKrgColumn();
                        for (int rowIdx = numRows - 1; rowIdx >= 0; -- rowIdx) {
                            if (krgCol[rowIdx] > 0.0) {
                                assert(rowIdx < numRows - 1);
                                m_criticalGasSat[tableIdx] =
                                    1.0 - slgofTable.getSlColumn()[rowIdx + 1];
                                break;
                            }
                        }

                        // find the critical oil saturation of the oil-gas system
                        const auto &kroOGCol = slgofTable.getKrogColumn();
                        for (int rowIdx = 0; rowIdx < numRows; ++rowIdx) {
                            if (kroOGCol[rowIdx] > 0.0) {
                                m_criticalOilOGSat[tableIdx] =
                                    slgofTable.getSlColumn()[rowIdx + 1];
                                break;
                            }
                        }
                    }
                }

                break;
            }
        }

        case SaturationFunctionFamily::FamilyII: {
            const TableContainer& swfnTables = tables->getSwfnTables();
            const TableContainer& sgfnTables = tables->getSgfnTables();
            const TableContainer& sof3Tables = tables->getSof3Tables();

            for (size_t tableIdx = 0; tableIdx < numSatTables; ++tableIdx) {
                {
                    const SwfnTable& swfnTable = swfnTables.getTable<SwfnTable>( tableIdx );
                    // find the critical water saturation
                    size_t numRows = swfnTable.numRows();
                    const auto &krwCol = swfnTable.getKrwColumn();
                    for (size_t rowIdx = 0; rowIdx < numRows; ++rowIdx) {
                        if (krwCol[rowIdx] > 0.0) {
                            double Sw = 0.0;
                            if (rowIdx > 0)
                                Sw = swfnTable.getSwColumn()[rowIdx - 1];
                            m_criticalWaterSat[tableIdx] = Sw;
                            break;
                        }
                    }
                }

                {
                    const SgfnTable& sgfnTable = sgfnTables.getTable<SgfnTable>( tableIdx );
                    // find the critical gas saturation
                    size_t numRows = sgfnTable.numRows();
                    const auto &krgCol = sgfnTable.getKrgColumn();
                    for (size_t rowIdx = 0; rowIdx < numRows; ++rowIdx) {
                        if (krgCol[rowIdx] > 0.0) {
                            double Sg = 0.0;
                            if (rowIdx > 0)
                                Sg = sgfnTable.getSgColumn()[rowIdx - 1];
                            m_criticalGasSat[tableIdx] = Sg;
                            break;
                        }
                    }
                }

                // find the critical oil saturation of the oil-gas system
                {
                    const Sof3Table& sof3Table = sof3Tables.getTable<Sof3Table>( tableIdx );
                    size_t numRows = sof3Table.numRows();
                    const auto &kroOGCol = sof3Table.getKrogColumn();
                    for (size_t rowIdx = 0; rowIdx < numRows; ++rowIdx) {
                        if (kroOGCol[rowIdx] > 0.0) {
                            double So = sof3Table.getSoColumn()[rowIdx - 1];
                            m_criticalOilOGSat[tableIdx] = So;
                            break;
                        }
                    }

                    // find the critical oil saturation of the water-oil system
                    const auto &kroOWCol = sof3Table.getKrowColumn();
                    for (size_t rowIdx = 0; rowIdx < numRows; ++rowIdx) {
                        if (kroOWCol[rowIdx] > 0.0) {
                            double So = sof3Table.getSoColumn()[rowIdx - 1];
                            m_criticalOilOWSat[tableIdx] = So;
                            break;
                        }
                    }
                }
            }
            break;

        }
        default:
            throw std::domain_error("No valid saturation keyword family specified");
        }


    }

    void findVerticalPoints( const EclipseState& m_eclipseState ) const {
        auto tables = m_eclipseState.getTableManager();
        auto tabdims = tables->getTabdims();
        size_t numSatTables = tabdims->getNumSatTables();

        m_maxPcog.resize( numSatTables , 0 );
        m_maxPcow.resize( numSatTables , 0 );
        m_maxKrg.resize( numSatTables , 0 );
        m_krgr.resize( numSatTables , 0 );
        m_maxKro.resize( numSatTables , 0 );
        m_krorw.resize( numSatTables , 0 );
        m_krorg.resize( numSatTables , 0 );
        m_maxKrw.resize( numSatTables , 0 );
        m_krwr.resize( numSatTables , 0 );

        switch (getSaturationFunctionFamily( m_eclipseState )) {
        case SaturationFunctionFamily::FamilyI:
        {
            const TableContainer& swofTables = tables->getSwofTables();
            const TableContainer& sgofTables = tables->getSgofTables();

            for (size_t tableIdx = 0; tableIdx < numSatTables; ++tableIdx) {
                const SwofTable& swofTable = swofTables.getTable<SwofTable>(tableIdx);
                const SgofTable& sgofTable = sgofTables.getTable<SgofTable>(tableIdx);
                // find the maximum output values of the oil-gas system
                m_maxPcog[tableIdx] = sgofTable.getPcogColumn().front();
                m_maxKrg[tableIdx] = sgofTable.getKrgColumn().back();

                m_krgr[tableIdx] = sgofTable.getKrgColumn().front();
                m_krwr[tableIdx] = swofTable.getKrwColumn().front();

                // find the oil relperm which corresponds to the critical water saturation
                const auto &krwCol = swofTable.getKrwColumn();
                const auto &krowCol = swofTable.getKrowColumn();
                for (size_t rowIdx = 0; rowIdx < krwCol.size(); ++rowIdx) {
                    if (krwCol[rowIdx] > 0.0) {
                        m_krorw[tableIdx] = krowCol[rowIdx - 1];
                        break;
                    }
                }

                // find the oil relperm which corresponds to the critical gas saturation
                const auto &krgCol = sgofTable.getKrgColumn();
                const auto &krogCol = sgofTable.getKrogColumn();
                for (size_t rowIdx = 0; rowIdx < krgCol.size(); ++rowIdx) {
                    if (krgCol[rowIdx] > 0.0) {
                        m_krorg[tableIdx] = krogCol[rowIdx - 1];
                        break;
                    }
                }

                // find the maximum output values of the water-oil system. the maximum oil
                // relperm is possibly wrong because we have two oil relperms in a threephase
                // system. the documentation is very ambiguos here, though: it says that the
                // oil relperm at the maximum oil saturation is scaled according to maximum
                // specified the KRO keyword. the first part of the statement points at
                // scaling the resultant threephase oil relperm, but then the gas saturation
                // is not taken into account which means that some twophase quantity must be
                // scaled.
                m_maxPcow[tableIdx] = swofTable.getPcowColumn().front();
                m_maxKro[tableIdx] = swofTable.getKrowColumn().front();
                m_maxKrw[tableIdx] = swofTable.getKrwColumn().back();
            }
            break;
        }
        case SaturationFunctionFamily::FamilyII: {
            const TableContainer& swfnTables = tables->getSwfnTables();
            const TableContainer& sgfnTables = tables->getSgfnTables();
            const TableContainer& sof3Tables = tables->getSof3Tables();

            for (size_t tableIdx = 0; tableIdx < numSatTables; ++tableIdx) {
                const Sof3Table& sof3Table = sof3Tables.getTable<Sof3Table>( tableIdx );
                const SgfnTable& sgfnTable = sgfnTables.getTable<SgfnTable>( tableIdx );
                const SwfnTable& swfnTable = swfnTables.getTable<SwfnTable>( tableIdx );

                // find the maximum output values of the oil-gas system
                m_maxPcog[tableIdx] = sgfnTable.getPcogColumn().back();
                m_maxKrg[tableIdx] = sgfnTable.getKrgColumn().back();

                // find the minimum output values of the relperm
                m_krgr[tableIdx] = sgfnTable.getKrgColumn().front();
                m_krwr[tableIdx] = swfnTable.getKrwColumn().front();

                // find the oil relperm which corresponds to the critical water saturation
                const double OilSatAtcritialWaterSat = 1.0 - m_criticalWaterSat[tableIdx] - m_minGasSat[tableIdx];
                m_krorw[tableIdx] = sof3Table.evaluate("KROW", OilSatAtcritialWaterSat);

                // find the oil relperm which corresponds to the critical gas saturation
                const double OilSatAtCritialGasSat = 1.0 - m_criticalGasSat[tableIdx] - m_minWaterSat[tableIdx];
                m_krorg[tableIdx] = sof3Table.evaluate("KROG", OilSatAtCritialGasSat);

                // find the maximum output values of the water-oil system. the maximum oil
                // relperm is possibly wrong because we have two oil relperms in a threephase
                // system. the documentation is very ambiguos here, though: it says that the
                // oil relperm at the maximum oil saturation is scaled according to maximum
                // specified the KRO keyword. the first part of the statement points at
                // scaling the resultant threephase oil relperm, but then the gas saturation
                // is not taken into account which means that some twophase quantity must be
                // scaled.
                m_maxPcow[tableIdx] = swfnTable.getPcowColumn().front();
                m_maxKro[tableIdx] = sof3Table.getKrowColumn().back();
                m_maxKrw[tableIdx] = swfnTable.getKrwColumn().back();
            }
            break;
        }

        default:
            throw std::domain_error("No valid saturation keyword family specified");
        }

    }
    // The saturation function family.
    // If SWOF and SGOF are specified in the deck it return FamilyI
    // If SWFN, SGFN and SOF3 are specified in the deck it return FamilyII
    // If keywords are missing or mixed, an error is given.
    SaturationFunctionFamily getSaturationFunctionFamily( const EclipseState& m_eclipseState ) const{
        auto tables = m_eclipseState.getTableManager();
        const TableContainer& swofTables = tables->getSwofTables();
        const TableContainer& sgofTables = tables->getSgofTables();
        const TableContainer& slgofTables = tables->getSlgofTables();
        const TableContainer& sof3Tables = tables->getSof3Tables();
        const TableContainer& swfnTables = tables->getSwfnTables();
        const TableContainer& sgfnTables = tables->getSgfnTables();


        bool family1 = (!sgofTables.empty() || !slgofTables.empty()) && !swofTables.empty();
        bool family2 = !swfnTables.empty() && !sgfnTables.empty() && !sof3Tables.empty();

        if (family1 && family2) {
            throw std::invalid_argument("Saturation families should not be mixed \n"
                                        "Use either SGOF (or SLGOF) and SWOF or SGFN, SWFN and SOF3");
        }

        if (!family1 && !family2) {
            throw std::invalid_argument("Saturations function must be specified using either "
                                        "family 1 or family 2 keywords \n"
                                        "Use either SGOF (or SLGOF) and SWOF or SGFN, SWFN and SOF3" );
        }

        if (family1 && !family2)
            return SaturationFunctionFamily::FamilyI;
        else if (family2 && !family1)
            return SaturationFunctionFamily::FamilyII;
        return SaturationFunctionFamily::noFamily; // no family or two families
    }



    double selectValue(const TableContainer& depthTables,
                       int tableIdx,
                       const std::string& columnName,
                       double cellDepth,
                       double fallbackValue,
                       bool useOneMinusTableValue) const {
        double value = fallbackValue;

        if (tableIdx >= 0) {
            const SimpleTable& table = depthTables.getTable( tableIdx );
            if (tableIdx >= static_cast<int>(depthTables.size()))
                throw std::invalid_argument("Not enough tables!");

            // evaluate the table at the cell depth
            value = table.evaluate(columnName, cellDepth);

            if (!std::isfinite(value))
                // a column can be fully defaulted. In this case, eval() returns a NaN
                // and we have to use the data from saturation tables
                value = fallbackValue;
            else if (useOneMinusTableValue)
                value = 1 - value;
        }

        return value;
    }

    mutable std::vector<double> m_criticalGasSat;
    mutable std::vector<double> m_criticalWaterSat;
    mutable std::vector<double> m_criticalOilOWSat;
    mutable std::vector<double> m_criticalOilOGSat;

    mutable std::vector<double> m_minGasSat;
    mutable std::vector<double> m_maxGasSat;
    mutable std::vector<double> m_minWaterSat;
    mutable std::vector<double> m_maxWaterSat;

    mutable std::vector<double> m_maxPcow;
    mutable std::vector<double> m_maxPcog;
    mutable std::vector<double> m_maxKrw;
    mutable std::vector<double> m_krwr;
    mutable std::vector<double> m_maxKro;
    mutable std::vector<double> m_krorw;
    mutable std::vector<double> m_krorg;
    mutable std::vector<double> m_maxKrg;
    mutable std::vector<double> m_krgr;
};


class SatnumEndpointInitializer : public EndpointInitializer {
public:
    void apply(std::vector<double>&, const Deck&, const EclipseState& ) const = 0;


    void satnumApply( std::vector<double>& values,
                      const std::string& columnName,
                      const std::vector<double>& fallbackValues,
                      const Deck& m_deck,
                      const EclipseState& m_eclipseState,
                      bool useOneMinusTableValue
                      ) const
    {
        auto eclipseGrid = m_eclipseState.getEclipseGrid();
        auto tables = m_eclipseState.getTableManager();
        auto tabdims = tables->getTabdims();
        auto satnum = m_eclipseState.getIntGridProperty("SATNUM");
        auto endnum = m_eclipseState.getIntGridProperty("ENDNUM");
        int numSatTables = tabdims->getNumSatTables();


        satnum->checkLimits(1 , numSatTables);

        // All table lookup assumes three-phase model
        assert( m_eclipseState.getNumPhases() == 3 );

        this->findSaturationEndpoints( m_eclipseState );
        this->findCriticalPoints( m_eclipseState );
        this->findVerticalPoints( m_eclipseState );

        // acctually assign the defaults. if the ENPVD keyword was specified in the deck,
        // this currently cannot be done because we would need the Z-coordinate of the
        // cell and we would need to know how the simulator wants to interpolate between
        // sampling points. Both of these are outside the scope of opm-parser, so we just
        // assign a NaN in this case...
        bool useEnptvd = m_deck.hasKeyword("ENPTVD");
        const auto& enptvdTables = tables->getEnptvdTables();
        for (size_t cellIdx = 0; cellIdx < eclipseGrid->getCartesianSize(); cellIdx++) {
            int satTableIdx = satnum->iget( cellIdx ) - 1;
            int endNum = endnum->iget( cellIdx ) - 1;
            double cellDepth = std::get<2>(eclipseGrid->getCellCenter(cellIdx));


            values[cellIdx] = this->selectValue(enptvdTables,
                                                (useEnptvd && endNum >= 0) ? endNum : -1,
                                                columnName ,
                                                cellDepth,
                                                fallbackValues[satTableIdx],
                                                useOneMinusTableValue);
        }
    }
};



class ImbnumEndpointInitializer : public EndpointInitializer {
public:

    void apply(std::vector<double>&, const Deck&, const EclipseState& ) const = 0;

    void imbnumApply( std::vector<double>& values,
                      const std::string& columnName,
                      const std::vector<double>& fallBackValues ,
                      const Deck& m_deck,
                      const EclipseState& m_eclipseState,
                      bool useOneMinusTableValue
                      ) const
    {
        auto eclipseGrid = m_eclipseState.getEclipseGrid();
        auto tables = m_eclipseState.getTableManager();
        auto imbnum = m_eclipseState.getIntGridProperty("IMBNUM");
        auto endnum = m_eclipseState.getIntGridProperty("ENDNUM");

        auto tabdims = tables->getTabdims();
        int numSatTables = tabdims->getNumSatTables();

        imbnum->checkLimits(1 , numSatTables);
        this->findSaturationEndpoints( m_eclipseState );
        this->findCriticalPoints( m_eclipseState );
        this->findVerticalPoints( m_eclipseState );

        // acctually assign the defaults. if the ENPVD keyword was specified in the deck,
        // this currently cannot be done because we would need the Z-coordinate of the
        // cell and we would need to know how the simulator wants to interpolate between
        // sampling points. Both of these are outside the scope of opm-parser, so we just
        // assign a NaN in this case...
        bool useImptvd = m_deck.hasKeyword("IMPTVD");
        const TableContainer& imptvdTables = tables->getImptvdTables();
        for (size_t cellIdx = 0; cellIdx < eclipseGrid->getCartesianSize(); cellIdx++) {
            int imbTableIdx = imbnum->iget( cellIdx ) - 1;
            int endNum = endnum->iget( cellIdx ) - 1;
            double cellDepth = std::get<2>(eclipseGrid->getCellCenter(cellIdx));

            values[cellIdx] = this->selectValue(imptvdTables,
                                                (useImptvd && endNum >= 0) ? endNum : -1,
                                                columnName,
                                                cellDepth,
                                                fallBackValues[imbTableIdx],
                                                useOneMinusTableValue);
        }
    }
};


/*****************************************************************/

class SGLEndpointInitializer : public SatnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->satnumApply(values , "SGCO" , this->m_minGasSat , deck, es, false);
    }
};


class ISGLEndpointInitializer : public ImbnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->imbnumApply(values , "SGCO" , this->m_minGasSat , deck, es, false);
    }
};

/*****************************************************************/

class SGUEndpointInitializer : public SatnumEndpointInitializer {
public:
    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->satnumApply(values , "SGMAX" , this->m_maxGasSat, deck, es, false);
    }
};


class ISGUEndpointInitializer : public ImbnumEndpointInitializer {
public:
    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->imbnumApply(values , "SGMAX" , this->m_maxGasSat , deck, es, false);
    }
};

/*****************************************************************/

class SWLEndpointInitializer : public SatnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->satnumApply(values , "SWCO" , this->m_minWaterSat , deck, es, false);
    }
};

class ISWLEndpointInitializer : public ImbnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->imbnumApply(values , "SWCO" , this->m_minWaterSat , deck, es, false);
    }
};

/*****************************************************************/

class SWUEndpointInitializer : public SatnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->satnumApply(values , "SWMAX" , this->m_maxWaterSat , deck, es, true);
    }
};

class ISWUEndpointInitializer : public ImbnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->imbnumApply(values , "SWMAX" , this->m_maxWaterSat , deck, es, true);
    }
};

/*****************************************************************/

class SGCREndpointInitializer : public SatnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->satnumApply(values , "SGCRIT" , this->m_criticalGasSat , deck, es, false);
    }
};

class ISGCREndpointInitializer : public ImbnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->imbnumApply(values , "SGCRIT" , this->m_criticalGasSat , deck, es, false);
    }
};

/*****************************************************************/

class SOWCREndpointInitializer : public SatnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->satnumApply(values , "SOWCRIT", this->m_criticalOilOWSat , deck, es, false);
    }
};

class ISOWCREndpointInitializer : public ImbnumEndpointInitializer
{
public:
    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->imbnumApply(values , "SOWCRIT" , this->m_criticalOilOWSat , deck, es, false);
    }
};

/*****************************************************************/

class SOGCREndpointInitializer : public SatnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->satnumApply(values , "SOGCRIT" , this->m_criticalOilOGSat , deck, es, false);
    }
};

class ISOGCREndpointInitializer : public ImbnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->imbnumApply(values , "SOGCRIT" , this->m_criticalOilOGSat , deck, es, false);
    }
};

/*****************************************************************/

class SWCREndpointInitializer : public SatnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->satnumApply(values , "SWCRIT" , this->m_criticalWaterSat , deck, es, false);
    }
};

class ISWCREndpointInitializer : public ImbnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->imbnumApply(values , "SWCRIT" , this->m_criticalWaterSat , deck, es, false);
    }
};

/*****************************************************************/

class PCWEndpointInitializer : public SatnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->satnumApply(values , "PCW" , this->m_maxPcow , deck, es, false);
    }
};

class IPCWEndpointInitializer : public ImbnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->imbnumApply(values , "IPCW" , this->m_maxPcow , deck, es, false);
    }
};


/*****************************************************************/

class PCGEndpointInitializer : public SatnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->satnumApply(values , "PCG" , this->m_maxPcog , deck, es, false);
    }
};

class IPCGEndpointInitializer : public ImbnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->imbnumApply(values , "IPCG" , this->m_maxPcog , deck, es, false);
    }
};

/*****************************************************************/

class KRWEndpointInitializer : public SatnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->satnumApply(values , "KRW" , this->m_maxKrw , deck, es, false);
    }
};

class IKRWEndpointInitializer : public ImbnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->imbnumApply(values , "IKRW" , this->m_maxKrw , deck, es, false);
    }
};


/*****************************************************************/

class KRWREndpointInitializer : public SatnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->satnumApply(values , "KRWR" , this->m_krwr , deck, es, false);
    }
};

class IKRWREndpointInitializer : public ImbnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->imbnumApply(values , "IKRWR" , this->m_krwr , deck, es, false);
    }
};

/*****************************************************************/

class KROEndpointInitializer : public SatnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->satnumApply(values , "KRO" , this->m_maxKro , deck, es, false);
    }
};

class IKROEndpointInitializer : public ImbnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->imbnumApply(values , "IKRO" , this->m_maxKro , deck, es, false);
    }
};


/*****************************************************************/

class KRORWEndpointInitializer : public SatnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->satnumApply(values , "KRORW" , this->m_krorw , deck, es, false);
    }
};

class IKRORWEndpointInitializer : public ImbnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->imbnumApply(values , "IKRORW" , this->m_krorw , deck, es, false);
    }
};

/*****************************************************************/

class KRORGEndpointInitializer : public SatnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->satnumApply(values , "KRORG" , this->m_krorg , deck, es, false);
    }
};

class IKRORGEndpointInitializer : public ImbnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->imbnumApply(values , "IKRORG" , this->m_krorg , deck, es, false);
    }
};

/*****************************************************************/

class KRGEndpointInitializer : public SatnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->satnumApply(values , "KRG" , this->m_maxKrg , deck, es, false);
    }
};

class IKRGEndpointInitializer : public ImbnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->imbnumApply(values , "IKRG" , this->m_maxKrg , deck, es, false);
    }
};

/*****************************************************************/

class KRGREndpointInitializer : public SatnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->satnumApply(values , "KRGR" , this->m_krgr , deck, es, false);
    }
};



class IKRGREndpointInitializer : public ImbnumEndpointInitializer {
public:

    void apply(std::vector<double>& values, const Deck& deck, const EclipseState& es ) const
    {
        this->imbnumApply(values , "IKRGR" , this->m_krgr , deck, es, false);
    }
};

}

#endif
