// For conditions of distribution and use, see copyright notice in license.txt

#ifndef incl_Interfaces_AssetServiceInterface_h
#define incl_Interfaces_AssetServiceInterface_h

#include "ServiceInterface.h"

namespace Foundation
{
    class AssetInterface;
    typedef boost::shared_ptr<AssetInterface> AssetPtr;
    
    class AssetProviderInterface;
    typedef boost::shared_ptr<AssetProviderInterface> AssetProviderPtr;    
    
    /*! An asset container service implements this interface to provide client modules
        with asset request and retrieval functionality.
        See \ref AssetModule "Using the asset module" for details on how to use the asset service.

        \ingroup Services_group
     */
    class AssetServiceInterface : public ServiceInterface
    {
    public:
        AssetServiceInterface() {}
        virtual ~AssetServiceInterface() {}

        //! Gets asset
        /*! If asset not in cache, will return empty pointer 
            Does not queue an asset download request.

            \param asset_id Asset ID, UUID for legacy UDP assets
            \param asset_type Asset type
            \return Pointer to asset           
         */
        virtual AssetPtr GetAsset(const std::string& asset_id, const std::string& asset_type) = 0;
        
        //! Gets incomplete asset
        /*! If not enough bytes received, will return empty pointer
            Does not queue an asset download request.
            
            \param asset_id Asset ID, UUID for legacy UDP assets
            \param asset_type Asset type
            \param received Minimum continuous bytes received from the start
            \return Pointer to asset
         */
        virtual AssetPtr GetIncompleteAsset(const std::string& asset_id, const std::string& asset_type, Core::uint received) = 0;
        
        //! Requests an asset download
        /*! Events will be sent when download progresses, and when asset is ready.
            Note that this will also send an ASSET_READY event even if the asset already exists in cache.

            \param asset_id Asset ID, UUID for legacy UDP assets
            \param asset_type Asset type
            \return non-zero request tag if download queued, 0 if not queued (no assetprovider could serve request) 
         */
        virtual Core::request_tag_t RequestAsset(const std::string& asset_id, const std::string& asset_type) = 0;

        //! Queries status of asset download
        /*! If asset has been already fully received, size, received & received_continuous will be the same
        
            \param asset_id Asset ID, UUID for legacy UDP assets
            \param size Variable to receive asset size (if known, 0 if unknown)
            \param received Variable to receive amount of bytes received
            \param received_continuous Variable to receive amount of continuous bytes received from the start
            \return true If asset was found either in cache or as a transfer in progress, and variables have been filled, false if not found
         */
        virtual bool QueryAssetStatus(const std::string& asset_id, Core::uint& size, Core::uint& received, Core::uint& received_continuous) = 0;

        //! Registers an asset provider
        /*! \param asset_provider Provider to register
            \return true if successfully registered
         */
        virtual bool RegisterAssetProvider(AssetProviderPtr asset_provider) = 0;
        
        //! Unregisters an asset provider
        /*! \param asset_provider Provider to unregister
            \return true if successfully unregistered
         */       
        virtual bool UnregisterAssetProvider(AssetProviderPtr asset_provider) = 0;
                
        //! Stores an asset to the asset cache
        /*! Typically called by AssetProviders when they complete a download of an asset. An event will be 
             sent to notify of the ready asset.
    
            \param asset Asset to store
         */
        virtual void StoreAsset(AssetPtr asset) = 0;
    };
}

#endif
