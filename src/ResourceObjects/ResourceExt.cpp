#include "ResourceObjects/Resource.h"
#include "Misc/Utility.h"

//-------------------------- modified: DeleteWithoutUpdatingOPF ----------------------
// This function deletes file for replacement, therefore it does not update opf.
bool Resource::DeleteWithoutUpdatingOPF()
{
    bool successful = false;
    {
        QWriteLocker locker(&m_ReadWriteLock);
        successful = Utility::SDeleteFile(m_FullFilePath);
    }

    if (successful) {
        emit Deleted(this);
        // try to prevent any resource modified signals from going out
        // while we wait for delete to actually happen
        disconnect(this, 0, 0, 0);
        deleteLater();
    }

    return successful;
}
//-------------------------------------------------------------------------------------